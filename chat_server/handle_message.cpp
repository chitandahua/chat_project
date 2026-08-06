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

#include "chat_server.hpp"
#include "config.hpp"
#include "grpc_service.hpp"
#include "handle_message.hpp"
#include "user_repo.hpp"

namespace asio = boost::asio;
namespace mysql = boost::mysql;
namespace http = boost::beast::http;
using namespace std::chrono_literals;

struct MessageData {
    const MsgNode& msg;

    std::shared_ptr<chat_session>& session;
    std::shared_ptr<RedisClient>& redis_client;
    mysql::connection_pool& mysql_pool;
    std::shared_ptr<grpc::Channel>& status_grpc_channel;

    UserRepo& user_repo;
};

MessageHandler::MessageHandler(std::shared_ptr<RedisClient>& redis_client,
                               std::shared_ptr<mysql::connection_pool>& pool)
    : redis_client_(redis_client), mysql_pool_(pool), user_repositoty_(pool) {}

MsgNode error_response(const MsgNode& msg) {
    // TODO 错误信息
    MsgNode res(msg.id(), nullptr);
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
                                         chat_session& session) {
    boost::redis::request set_req;
    set_req.push("HSET", login_count_key, session.server_name(), session.server_session_count());
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
    std::cout << "update server login count: " << session.server_name()
              << " count: " << session.server_session_count() << "\n";
    co_return true;
}

asio::awaitable<MsgNode> login(const MessageData& input) {
    auto request = nlohmann_parse_json<UserLoginRequest>(input.msg.body());
    if (!request) {
        co_return error_response(input.msg);
    }

    // 从status grpc获取token
    auto response = LoginServiceClient::get_login_token(
        input.status_grpc_channel, LoginMsgRequest{request.value().uid, request.value().token});
    if (!response || response.value().error() != 0) {
        co_return error_response(input.msg);
    }

    std::cout << "login response: " << response.value().token() << "\n";
    // 从内存/数据库中获取UserInfo
    auto uid = request.value().uid;
    auto user_info = input.user_repo.get_user_info_memory(uid);
    if (!user_info) {
        auto result = co_await input.user_repo.get_user_info(uid);
        if (!result) {
            co_return error_response(input.msg);
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
    co_return MsgNode(input.msg.id(), json_response.dump());
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

asio::awaitable<MsgNode> MessageHandler::handle_message(std::shared_ptr<chat_session> session,
                                                        const MsgNode& msg) {
    // TODO 获取id
    auto it = handlers_.find(MessageId::Login);
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
    handlers_.insert({MessageId::Login, login});
}
