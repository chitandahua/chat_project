#include <boost/redis/request.hpp>
#include <boost/url.hpp>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

#include <boost/asio/detached.hpp>

#include <boost/mysql/any_address.hpp>
#include <boost/mysql/pool_params.hpp>

#include <iostream>
#include <optional>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>

#include <boost/describe/class.hpp>

#include <magic_enum/magic_enum.hpp>

#include "chat_server.hpp"
#include "config.hpp"
#include "grpc_service.hpp"
#include "handle_message.hpp"
#include "user_repo.hpp"

namespace asio = boost::asio;
namespace mysql = boost::mysql;
namespace redis = boost::redis;
namespace http = boost::beast::http;
using namespace std::chrono_literals;

struct MessageData {
    const MsgNode& msg;

    std::shared_ptr<ChatSession>& session;
    std::shared_ptr<RedisClient>& redis_client;
    mysql::connection_pool& mysql_pool;
    std::shared_ptr<grpc::Channel>& status_grpc_channel;

    UserRepo& user_repo;
};

MessageHandler::MessageHandler(std::shared_ptr<RedisClient>& redis_client,
                               std::shared_ptr<mysql::connection_pool>& pool)
    : redis_client_(redis_client), mysql_pool_(pool), user_repositoty_(pool) {}

// TODO 错误信息
MsgNode error_response(const MsgNode& msg) {
    MsgNode res(msg.id(), nullptr);
    return res;
}

MsgNode error_response(int response_id) {
    // TODO 错误信息
    MsgNode res(response_id, nullptr);
    return res;
}

// TODO 放到公共库
template <typename T>
concept JsonSerializable = requires(const T& t, nlohmann::json& j) { to_json(j, t); };

template <typename T>
concept JsonDeserializable = requires(const nlohmann::json& j, T& t) { from_json(j, t); };

template <JsonSerializable T>
http::response<http::string_body> nlohmann_json_response(const T& body) {
    http::response<http::string_body> res;

    res.set("Content-Type", "application/json");
    res.body() = nlohmann::json(body).dump();

    return res;
}

template <JsonDeserializable T>
tl::expected<T, std::string> nlohmann_parse_json(std::string_view json_string) {
    try {
        auto json = nlohmann::json::parse(json_string);
        return json.get<T>();
    } catch (const nlohmann::json::exception& e) {
        return tl::make_unexpected(e.what());
    }
}

nlohmann::json response_payload(std::optional<nlohmann::json> body = std::nullopt, int err = 0,
                                const std::string& msg = "") {
    nlohmann::json j = {};
    j["status"] = static_cast<uint8_t>(err);
    j["message"] = msg;
    if (body.has_value()) {
        j["data"] = std::move(body.value());
    }
    return j;
}

nlohmann::json response_payload_empty(int err = 0, const std::string& msg = "") {
    return response_payload(std::nullopt, err, msg);
}

constexpr std::string_view login_count_key = "login_count";
asio::awaitable<bool> update_login_count(std::shared_ptr<RedisClient>& redis_client,
                                         ChatSession& session) {
    boost::redis::request set_req;
    set_req.push("HSET", login_count_key, session.server().name(),
                 session.server().participant_count());
    boost::redis::response<long long> set_resp;
    boost::system::error_code ec;
    co_await redis_client->conn_->async_exec(
        set_req, set_resp,
        boost::asio::redirect_error(boost::asio::cancel_after(10s, boost::asio::use_awaitable),
                                    ec));
    if (ec) {
        std::cerr << "redis set error: " << ec.message() << std::endl;
        co_return false;
    }
    std::cout << "update server login count: " << session.server().name()
              << " count: " << session.server().participant_count() << "\n";
    co_return true;
}

asio::awaitable<MsgNode> login(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::LoginResponse);
    auto request = nlohmann_parse_json<UserLoginRequest>(input.msg.body());
    if (!request) {
        co_return error_response(response_id);
    }

    // 从status grpc获取token
    auto response = LoginServiceClient::get_login_token(
        input.status_grpc_channel, LoginMsgRequest{request.value().uid, request.value().token});
    if (!response || response.value().error() != 0) {
        co_return error_response(response_id);
    }

    std::cout << "login response: " << response.value().token() << "\n";
    // 从内存/数据库中获取UserInfo
    auto uid = request.value().uid;
    auto user_info = input.user_repo.get_user_info_memory(uid);
    if (!user_info) {
        auto result = co_await input.user_repo.get_user_info(uid);
        if (!result) {
            co_return error_response(response_id);
        }
        user_info = result.value();
    }

    std::cout << "user info: " << user_info->name << "\n";
    // 登录数 更新到redis
    bool ok = co_await update_login_count(input.redis_client, *input.session);
    if (!ok) {
        std::cout << "warning: update login count failed\n";
    }

    // session更新uid
    std::cout << "update session uid: " << uid << "\n";
    input.session->set_uid(uid);
    // TODO 为用户设置登录ip server的名字 redis SET uid映射server name

    (void)input.user_repo.set_user_info_memory(uid, user_info.value());
    UserLoginResponse login_response{uid, response.value().token(), user_info.value().name};
    auto json_response = response_payload(nlohmann::json(login_response));
    std::cout << "login response: " << json_response.dump() << "\n";
    co_return MsgNode(response_id, json_response.dump());
}

// boost::json::serialize(boost::json::value_from(body));

// Attempts to parse a string as a JSON into an object of type T.
// T should be a type with Boost.Describe metadata.
template <class T>
boost::system::result<T> parse_json(std::string_view json_string) {
    boost::system::error_code ec;
    auto val = boost::json::parse(json_string, ec);
    if (ec) {
        return ec;
    }

    return boost::json::try_value_to<T>(val);
}

template <typename T>
asio::awaitable<std::optional<UserInfo>> search_user_info(const MessageData& input,
                                                          const std::string& user_info_key,
                                                          const T& key, bool& is_redis_exist) {
    // 先查redis
    redis::request get_req;
    get_req.push("GET", user_info_key);
    redis::response<std::optional<std::string>> get_resp;

    boost::system::error_code ec;
    co_await input.redis_client->conn_->async_exec(
        get_req, get_resp, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis get error: " << ec.message() << std::endl;
        co_return std::nullopt;
    }

    auto result = std::get<0>(get_resp);
    if (result.has_error()) {
        co_return std::nullopt;
    }

    const std::optional<std::string>& opt_value = result.value();
    if (opt_value.has_value()) {
        auto user_info = parse_json<UserInfo>(opt_value.value());
        if (user_info) {
            is_redis_exist = true;
            co_return user_info.value();
        }
    }

    std::cout << "redis not exist: " << user_info_key << ", search mysql\n";
    // 没有则查mysql
    auto user_info_mysql = co_await input.user_repo.get_user_info(key);
    if (!user_info_mysql) {
        co_return std::nullopt;
    }
    co_return user_info_mysql.value();
}

asio::awaitable<void> save_user_info(const MessageData& input, const std::string& key,
                                     const std::string& user_info_str) {
    redis::request set_req;
    set_req.push("SET", key, user_info_str);
    redis::response<std::string> set_resp;
    boost::system::error_code ec;
    co_await input.redis_client->conn_->async_exec(
        set_req, set_resp, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis set error: " << ec.message() << std::endl;
    }
    co_return;
}

template <typename T>
awaitable<std::optional<MsgNode>> search_user_by_type(const MessageData& input, const T& key) {
    std::string user_info_key;
    if constexpr (std::is_same_v<T, int64_t>) {
        user_info_key = std::string(UserUidInfoPrefix) + std::to_string(key);
    } else if constexpr (std::is_same_v<T, std::string>) {
        user_info_key = std::string(UserNameInfoPrefix) + key;
    } else {
        static_assert(sizeof(T) == 0, "Unsupported type for search_user_by_type");
    }

    bool is_redis_exist = false;
    auto result = co_await search_user_info<T>(input, user_info_key, key, is_redis_exist);
    if (!result) {
        co_return std::nullopt;
    }
    std::cout << "search user info: " << user_info_key << " " << result.value().name << "\n";
    auto user_info_str = boost::json::serialize(boost::json::value_from(result.value()));
    if (!is_redis_exist) {
        // 保存到redis
        co_await save_user_info(input, user_info_key, user_info_str);
    }
    co_return MsgNode(input.msg.id(), user_info_str);
}

awaitable<MsgNode> search_user(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::SearchUserResponse);
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(input.msg.body());
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "parse json error: " << e.what() << "\n";
        co_return error_response(response_id);
    }

    std::optional<MsgNode> response;
    if (request.contains("uid") && request["uid"].is_number()) {
        response = co_await search_user_by_type<int64_t>(input, request["uid"].get<int64_t>());
    } else if (request.contains("name") && request["name"].is_string()) {
        response =
            co_await search_user_by_type<std::string>(input, request["name"].get<std::string>());
    }

    if (response) {
        response.value().set_id(response_id);
    }
    co_return response.value_or(error_response(response_id));
}

void log_mysql_error(boost::system::error_code ec, const mysql::diagnostics& diag) {
    std::cerr << "MySQL error: " << ec << " " << ec.message();

    if (!diag.client_message().empty()) {
        std::cerr << ": " << diag.client_message();
    }

    if (!diag.server_message().empty()) {
        std::cerr << ": " << diag.server_message();
    }

    std::cerr << std::endl;
}

asio::awaitable<MsgNode> MessageHandler::handle_message(std::shared_ptr<ChatSession> session,
                                                        const MsgNode& msg) {
    auto message_id = magic_enum::enum_cast<MessageId>(msg.id());
    if (!message_id) {
        co_return error_response(msg);
    }

    auto it = handlers_.find(message_id.value());
    if (it == handlers_.end()) {
        co_return error_response(msg);
    }

    try {
        co_return co_await it->second(MessageData{msg, session, redis_client_, *mysql_pool_,
                                                  status_grpc_channel_, user_repositoty_});
    } catch (const mysql::error_with_diagnostics& err) {
        log_mysql_error(err.code(), err.get_diagnostics());

        co_return error_response(msg);
    } catch (const std::exception& err) {
        std::cerr << "Uncaught exception: " << err.what() << std::endl;
        co_return error_response(msg);
    }
}

int MessageHandler::init(const ChatConfig& config) {
    // status grpc
    auto status_target =
        config.status_server.host + ":" + std::to_string(config.status_server.port);
    status_grpc_channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(status_target, grpc::InsecureChannelCredentials()));
    if (!status_grpc_channel_) {
        std::cerr << "Failed to create status gRPC channel. Invalid address or port." << "\n";
        return -1;
    }

    // mysql
    // mysql_conn_->init(config.mysql_config);
    // mysql_pool_->pool()->async_run(asio::detached);

    handlers_init();
    return 0;
}

void MessageHandler::handlers_init() {
    handlers_.insert({MessageId::LoginRequest, login});
    handlers_.insert({MessageId::SearchUserRequest, search_user});
}
