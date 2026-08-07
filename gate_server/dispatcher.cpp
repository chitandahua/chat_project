#include <boost/url.hpp>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

#include <boost/asio/detached.hpp>

#include <boost/mysql/any_address.hpp>
#include <boost/mysql/pool_params.hpp>

#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>

#include <boost/describe/class.hpp>
#include <boost/redis/src.hpp>

#include "config.hpp"
#include "dispatcher.hpp"
#include "error.hpp"
#include "http_connection.hpp"
#include "mysql_conn_pool.hpp"
#include "user_repo.hpp"

namespace asio = boost::asio;
namespace mysql = boost::mysql;
namespace http = boost::beast::http;

void log_mysql_error(boost::system::error_code ec, const mysql::diagnostics& diag) {
    auto guard = lock_cerr();

    std::cerr << "MySQL error: " << ec << " " << ec.message();

    if (!diag.client_message().empty()) {
        std::cerr << ": " << diag.client_message();
    }

    if (!diag.server_message().empty()) {
        std::cerr << ": " << diag.server_message();
    }

    std::cerr << std::endl;
}

// Attempts to parse a numeric ID from a string
std::optional<std::int64_t> parse_id(std::string_view from) {
    std::int64_t id{};
    auto res = std::from_chars(from.data(), from.data() + from.size(), id);
    if (res.ec != std::errc{} || res.ptr != from.data() + from.size())
        return std::nullopt;
    return id;
}

http::response<http::string_body> error_response(http::status code, std::string_view msg) {
    http::response<http::string_body> res;
    res.result(code);
    res.body() = msg;
    res.prepare_payload();
    return res;
}

// Like error_response, but always uses a 400 status code
http::response<http::string_body> bad_request(std::string_view body) {
    return error_response(http::status::bad_request, body);
}

// Like error_response, but always uses a 500 status code and
// never provides extra information that might help potential attackers.
http::response<http::string_body> internal_server_error() {
    return error_response(http::status::internal_server_error, "Internal server error");
}

// Creates a response with a serialized JSON body.
// T should be a type with Boost.Describe metadata containing the
// body data to be serialized
template <class T>
http::response<http::string_body> json_response(const T& body) {
    http::response<http::string_body> res;

    res.set("Content-Type", "application/json");
    res.body() = boost::json::serialize(boost::json::value_from(body));
    res.prepare_payload();
    return res;
}

// Attempts to parse a string as a JSON into an object of type T.
// T should be a type with Boost.Describe metadata.
template <class T>
boost::system::result<T> parse_json(std::string_view json_string) {
    // Attempt to parse the request into a json::value.
    // This will fail if the provided body isn't valid JSON.
    boost::system::error_code ec;
    auto val = boost::json::parse(json_string, ec);
    if (ec) {
        return ec;
    }

    // Attempt to parse the json::value into a T. This will
    // fail if the provided JSON doesn't match T's shape.
    return boost::json::try_value_to<T>(val);
}

template <typename T>
concept JsonSerializable = requires(const T& t, nlohmann::json& j) { to_json(j, t); };

template <typename T>
concept JsonDeserializable = requires(const nlohmann::json& j, T& t) { from_json(j, t); };

template <JsonSerializable T>
http::response<http::string_body> nlohmann_json_response(const T& body) {
    http::response<http::string_body> res;

    res.set("Content-Type", "application/json");
    res.body() = nlohmann::json(body).dump();
    res.prepare_payload();
    return res;
}

http::response<http::string_body> nlohmann_json_response(nlohmann::json&& body) {
    http::response<http::string_body> res;

    res.set("Content-Type", "application/json");
    res.body() = std::move(body.dump());
    res.prepare_payload();
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

http::response<http::string_body> response_from_db_error(boost::system::error_code ec) {
    if (ec.category() == get_service_category()) {
        switch (static_cast<ErrorCode>(ec.value())) {
            case ErrorCode::NOT_FOUND:
                return error_response(http::status::not_found, "user does not exist");
            case ErrorCode::USER_OR_EMAIL_EXIST:
                return error_response(http::status::conflict, "user or email conflict");
            default:
                return internal_server_error();
        }
    } else {
        return internal_server_error();
    }
}

// Contains data associated to an HTTP request.
// To be passed to individual handler functions
struct RequestData {
    const http::request<http::string_body>& request;
    boost::urls::url_view target;

    mysql::connection_pool& mysql_pool;
    boost::redis::connection& redis_conn;
    std::shared_ptr<grpc::Channel>& grpc_channel;
    std::shared_ptr<grpc::Channel>& status_grpc_channel;

    UserRepo user_repo;
};

Dispatcher::Dispatcher(std::shared_ptr<boost::asio::io_context>& ioc)
    : redis_conn_(std::make_shared<boost::redis::connection>(*ioc)),
      mysql_conn_(std::make_shared<MysqlConnPool>(ioc)) {}

asio::awaitable<http::response<http::string_body>> get_test(const RequestData& input) {
    // auto ostream = boost::beast::ostream(res.body());
    std::ostringstream ostream;
    ostream << "receive get_test req" << "\n";
    for (const auto& param : input.target.params()) {
        ostream << param.key << " = " << param.value << '\n';
    }

    http::response<http::string_body> res;
    res.result(http::status::ok);
    res.set(http::field::content_type, "text/plain");
    res.body() = ostream.str();
    res.prepare_payload();
    co_return res;
}

struct GetVerifyCodeRequest {
    std::string email;
};
BOOST_DESCRIBE_STRUCT(GetVerifyCodeRequest, (), (email))

struct GetVerifyCodeResponse {
    std::string email;
    std::string code;
};
BOOST_DESCRIBE_STRUCT(GetVerifyCodeResponse, (), (email, code))

asio::awaitable<http::response<http::string_body>> get_verify_code(const RequestData& input) {
    auto it = input.request.find("Content-Type");
    if (it == input.request.end() || it->value() != "application/json") {
        co_return bad_request("Invalid Content-Type: expected 'application/json'");
    }

    // Parse the request body
    auto req = parse_json<GetVerifyCodeRequest>(input.request.body());
    // nlohmann::json::parse(input.request.body);
    if (req.has_error()) {
        co_return bad_request("Invalid JSON body");
    }

    // TODO 改为异步
    VerifyServiceClient verify_service_client(input.grpc_channel);
    auto rpc_result = verify_service_client.get_verify_code(req.value().email);
    if (!rpc_result) {
        co_return internal_server_error();
    } else if (rpc_result.value().error() != 0) {
        // std::cerr << "rpc result error: " << rpc_result.value().error() << "\n";
        co_return internal_server_error();
        // co_return nlohmann_json_response(response_payload_empty(ServiceError::RPC_RETURN_ERROR));
    }

    auto res = GetVerifyCodeResponse{req.value().email, rpc_result.value().code()};
    co_return json_response(res);
}

nlohmann::json response_payload(std::optional<nlohmann::json> body = std::nullopt,
                                ServiceError err = ServiceError::SUCCESS,
                                const std::string& msg = "") {
    nlohmann::json j = {};
    j["status"] = static_cast<uint8_t>(err);
    j["message"] = msg.empty() ? ServiceError2String(err) : msg;
    if (body.has_value()) {
        j["data"] = std::move(body.value());
    }
    return j;
}

nlohmann::json response_payload_empty(ServiceError err = ServiceError::SUCCESS,
                                      const std::string& msg = "") {
    return response_payload(std::nullopt, err, msg);
}

asio::awaitable<http::response<http::string_body>> user_register(const RequestData& input) {
    auto request = nlohmann_parse_json<UserRegisterRequest>(input.request.body());
    if (!request) {
        co_return bad_request("Invalid JSON body");
    }

    // 查询redis verify code是否匹配
    boost::redis::request req;
    req.push("GET", request.value().email);
    boost::redis::response<std::optional<std::string>> resp;

    // TODO timeout
    co_await input.redis_conn.async_exec(req, resp);
    auto result = std::get<0>(resp).value();
    if (!result || result.value() != request.value().verify_code) {
        co_return nlohmann_json_response(response_payload_empty(ServiceError::INVALID_VERIFY_CODE));
    }

    std::cout << "redis verify code match\n";
    auto user_info = co_await input.user_repo.create_user(request.value());
    if (!user_info && user_info.error() == ServiceError::USER_OR_EMAIL_EXIST) {
        co_return nlohmann_json_response(response_payload_empty(ServiceError::USER_OR_EMAIL_EXIST));
    }

    co_return nlohmann_json_response(response_payload(user_info.value()));
}

asio::awaitable<http::response<http::string_body>> reset_password(const RequestData& input) {
    auto request = nlohmann_parse_json<UserResetPasswordRequest>(input.request.body());
    if (!request) {
        co_return bad_request("Invalid JSON body");
    }

    // 查询redis verify code是否匹配
    boost::redis::request req;
    req.push("GET", request.value().email);
    boost::redis::response<std::optional<std::string>> resp;

    // TODO timeout
    co_await input.redis_conn.async_exec(req, resp);
    auto result = std::get<0>(resp).value();
    if (!result || result.value() != request.value().verify_code) {
        co_return nlohmann_json_response(response_payload_empty(ServiceError::INVALID_VERIFY_CODE));
    }

    std::cout << "redis verify code match\n";
    auto success = co_await input.user_repo.reset_password(request.value());
    if (!success) {
        co_return nlohmann_json_response(
            response_payload_empty(ServiceError::USER_OR_EMAIL_INVALID));
    }

    co_return nlohmann_json_response(response_payload_empty());
}

class UserLoginResponse {
public:
    int64_t id;
    std::string user;
    std::string token;
    std::string host;
    int port;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserLoginResponse, id, user, token, host, port)
};

asio::awaitable<http::response<http::string_body>> user_login(const RequestData& input) {
    auto request = nlohmann_parse_json<UserLoginRequest>(input.request.body());
    if (!request) {
        co_return bad_request("Invalid JSON body");
    }

    auto result = co_await input.user_repo.check_password(request.value());
    if (!result) {
        co_return nlohmann_json_response(response_payload_empty(result.error()));
    }

    // grpc获取token和host
    // TODO 改为异步
    auto response = StatusServiceClient::get_chat_server(input.status_grpc_channel, result.value());
    if (!response) {
        co_return internal_server_error();
        // co_return nlohmann_json_response(response_payload_empty(response.error()));
    } else if (response.value().error() != 0) {
        co_return nlohmann_json_response(response_payload_empty(ServiceError::RPC_RETURN_ERROR));
    }

    co_return nlohmann_json_response(response_payload(
        UserLoginResponse{result.value(), request.value().user, response.value().token(),
                          response.value().host(), response.value().port()}));
}

asio::awaitable<http::response<http::string_body>> Dispatcher::handle_request(
    const http::request<http::string_body>& request) {
    auto target = boost::urls::parse_origin_form(request.target());
    if (!target.has_value()) {
        co_return bad_request("Invalid request target");
    }

    auto [it1, it2] = handlers_.equal_range(target->path());
    if (it1 == handlers_.end()) {
        co_return error_response(http::status::not_found, "The requested endpoint does not exist");
    }

    // Match the verb. The table structure that we created
    // allows us to distinguish between an "endpoint does not exist" error
    // and an "unsupported method" error.
    auto it3 =
        std::find_if(it1, it2, [&request](const std::pair<std::string_view, HttpHandler>& handler) {
            return handler.second.method == request.method();
        });
    if (it3 == it2) {
        co_return error_response(http::status::method_not_allowed, "Unsupported HTTP method");
    }

    // Invoke the handler
    try {
        // Attempt to handle the request
        co_return co_await it3->second.handler(
            RequestData{request, *target, *mysql_conn_->pool(), *redis_conn_, grpc_channel_,
                        status_grpc_channel_, UserRepo(mysql_conn_->pool())});
    } catch (const mysql::error_with_diagnostics& err) {
        // A Boost.MySQL error. This will happen if you don't have connectivity
        // to your database, your schema is incorrect or your credentials are invalid.
        // Log the error, including diagnostics
        log_mysql_error(err.code(), err.get_diagnostics());

        // Never disclose error info to a potential attacker
        co_return internal_server_error();
    } catch (const std::exception& err) {
        // Another kind of error. This indicates a programming error or a severe
        // server condition (e.g. out of memory). Same procedure as above.
        {
            auto guard = lock_cerr();
            std::cerr << "Uncaught exception: " << err.what() << std::endl;
        }
        co_return internal_server_error();
    }
}

void redis_config_init(const RedisConfig& redis_config, boost::redis::config& cfg) {
    if (!redis_config.user.empty()) {
        cfg.use_setup = true;
        cfg.setup.clear();
        cfg.setup.hello(redis_config.user, redis_config.pass);
    }

    cfg.addr.host = redis_config.host;
    cfg.addr.port = std::to_string(redis_config.port);
}

int Dispatcher::init(const Config& config) {
    // verify grpc
    auto target =
        config.verify_server_config.host + ":" + std::to_string(config.verify_server_config.port);
    grpc_channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    if (!grpc_channel_) {
        std::cerr << "Failed to create verify gRPC channel. Invalid address or port." << "\n";
        return -1;
    }
    // status grpc
    auto status_target =
        config.status_server_config.host + ":" + std::to_string(config.status_server_config.port);
    status_grpc_channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(status_target, grpc::InsecureChannelCredentials()));
    if (!status_grpc_channel_) {
        std::cerr << "Failed to create status gRPC channel. Invalid address or port." << "\n";
        return -1;
    }

    // redis
    boost::redis::config redis_cfg;
    redis_config_init(config.redis_config, redis_cfg);
    redis_conn_->async_run(redis_cfg, asio::consign(asio::detached, redis_conn_));

    // mysql
    mysql_conn_->init(config.mysql_config);
    mysql_conn_->pool()->async_run(asio::detached);

    handlers_init();
    return 0;
}

void Dispatcher::handlers_init() {
    handlers_.insert({"/get_test", HttpHandler(http::verb::get, get_test)});
    handlers_.insert({"/get_verify_code", HttpHandler(http::verb::post, get_verify_code)});
    handlers_.insert({"/user_register", HttpHandler(http::verb::post, user_register)});
    handlers_.insert({"/reset_password", HttpHandler(http::verb::post, reset_password)});
    handlers_.insert({"/user_login", HttpHandler(http::verb::post, user_login)});
}

void Dispatcher::register_get_handler(const std::string& path, RequestHandler&& handler) {
    handlers_.insert({path, HttpHandler(http::verb::get, std::move(handler))});
}

void Dispatcher::register_post_handler(const std::string& path, RequestHandler&& handler) {
    handlers_.insert({path, HttpHandler(http::verb::post, std::move(handler))});
}