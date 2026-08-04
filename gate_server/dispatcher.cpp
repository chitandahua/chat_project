#include <boost/url.hpp>
#include <memory>
#include <nlohmann/json.hpp>

#include <boost/asio/detached.hpp>

#include <boost/mysql/any_address.hpp>
#include <boost/mysql/pool_params.hpp>

#include <iostream>

#include "config.hpp"
#include "dispatcher.hpp"
#include "http_connection.hpp"
#include "mysql_conn_pool.hpp"
#include "user_repo.hpp"

#include <boost/json/serialize.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>

#include <boost/redis/src.hpp>

namespace asio = boost::asio;
namespace mysql = boost::mysql;

Dispatcher::Dispatcher(boost::asio::io_context& ioc)
    : redis_conn_(std::make_shared<boost::redis::connection>(ioc)),
      mysql_conn_(std::make_shared<MysqlConnPool>(ioc)) {}

ErrorCode get_test(std::shared_ptr<HttpConnection>& connection) {
    auto uri = boost::urls::parse_origin_form(connection->request().target());
    if (!uri) {
        return ErrorCode::FAILED;
    }

    auto ostream = boost::beast::ostream(connection->response().body());
    ostream << "receive get_test req" << "\n";
    auto& view = uri.value();
    for (const auto& param : view.params()) {
        ostream << param.key << " = " << param.value << '\n';
    }

    return ErrorCode::SUCCESS;
}

ErrorCode get_verify_code(std::shared_ptr<HttpConnection>& connection,
                          const std::shared_ptr<Dispatcher::GrpcClient>& grpc_client) {
    auto body = boost::beast::buffers_to_string(connection->request().body().data());
    std::cout << "body: " << body << "\n";

    try {
        auto json = nlohmann::json::parse(body);
        auto email = json["email"];
        auto rpc_result = grpc_client->verify_service_client_->get_verify_code(email);

        nlohmann::json result;
        if (rpc_result) {
            result = {
                "status",
                static_cast<uint8_t>(ErrorCode::SUCCESS),
                "data",
                {{"email", email}, {"code", rpc_result.value().code()}},
            };
        } else {
            result = {
                "status",
                static_cast<uint8_t>(ErrorCode::RPC_FAILED),
                "data",
                {{"email", email}},
            };
        }

        connection->response().set(http::field::content_type, "application/json");
        boost::beast::ostream(connection->response().body()) << result.dump() << "\r\n";
    } catch (const nlohmann::json::parse_error& e) {
        std::cout << "parse error: " << e.what() << "\n";
        return ErrorCode::INVALID_JSON;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode user_register(std::shared_ptr<HttpConnection>& connection,
                        std::shared_ptr<boost::mysql::connection_pool> mysql_conn,
                        std::shared_ptr<boost::redis::connection> redis_conn) {
    auto body = boost::beast::buffers_to_string(connection->request().body().data());
    std::cout << "body: " << body << "\n";

    UserRegisterRequest request;
    try {
        request = nlohmann::json::parse(body).get<UserRegisterRequest>();
    } catch (const nlohmann::json::parse_error& e) {
        std::cout << "parse error: " << e.what() << "\n";
        return ErrorCode::INVALID_JSON;
    }

    // 查询redis verify code是否匹配
    boost::redis::request req;
    req.push("GET", request.email);
    boost::redis::response<std::optional<std::string>> resp;

    // TODO 改成异步
    // 用 use_future,阻塞等结果,不用改成协程
    // 致命前提:调用 .get() 这个动作,必须发生在跟"驱动这个 io_context 事件循环"不同的线程上。
    // 此处get()是在子线程的io_context, redis_conn_使用的是main的io_context
    auto fut = redis_conn->async_exec(req, resp, boost::asio::use_future);
    fut.get();  // 阻塞在这里,等 redis 操作真正完成

    auto result = std::get<0>(resp).value();
    if (!result || result.value() != request.verify_code) {
        return ErrorCode::INVALID_VERIFY_CODE;
    }

    // 查数据库,异步接口,同样模式
    auto user_repo = UserRepo(mysql_conn);
    auto user_info = user_repo.create_user(request);
    // TODO 修改create_user 返回error为状态码
    if (!user_info && user_info.error() == "user or email exist") {
        return ErrorCode::USER_OR_EMAIL_EXIST;
    }

    if (user_info) {
        boost::beast::ostream(connection->response().body())
            << boost::json::serialize(boost::json::value_from(user_info.value())) << "\r\n";
    } else {
        return ErrorCode::FAILED;
    }
    return ErrorCode::SUCCESS;
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
    auto target =
        config.rpc_server_config.host + ":" + std::to_string(config.rpc_server_config.port);
    channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    if (!channel_) {
        std::cerr << "Failed to create gRPC channel. Invalid address or port." << "\n";
        return -1;
    }

    boost::redis::config redis_cfg;
    redis_config_init(config.redis_config, redis_cfg);
    // redis_conn_->async_run(redis_cfg, asio::detached);
    redis_conn_->async_run(redis_cfg, [](boost::system::error_code ec) {
        if (ec) {
            std::cout << "redis conn run error:" << ec.message() << '\n';
        }
    });

    mysql_conn_->init(config.mysql_config);
    mysql_conn_->pool()->async_run(asio::detached);

    grpc_client_ = std::make_shared<GrpcClient>();
    grpc_client_->verify_service_client_ = std::make_unique<VerifyServiceClient>(channel_);

    register_get_handler("/get_test", get_test);
    register_post_handler("/get_verify_code", [client = grpc_client_](auto conn) -> ErrorCode {
        return get_verify_code(conn, client);
    });
    register_post_handler(
        "/user_register",
        [mysql_conn = mysql_conn_->pool(), redis_conn = redis_conn_](auto conn) -> ErrorCode {
            return user_register(conn, mysql_conn, redis_conn);
        });

    return 0;
}

ErrorCode Dispatcher::handle_get_request(std::shared_ptr<HttpConnection> conn,
                                         const std::string& path) {
    auto it = get_handlers_.find(path);
    if (it != get_handlers_.end()) {
        return it->second(conn);
    }
    std::cerr << "invalid get request path: " << path << "\n";
    return ErrorCode::NOT_FOUND;
}

void Dispatcher::register_get_handler(const std::string& path, HttpHandler handler) {
    get_handlers_[path] = std::move(handler);
}

ErrorCode Dispatcher::handle_post_request(std::shared_ptr<HttpConnection> conn,
                                          const std::string& path) {
    auto it = post_handlers_.find(path);
    if (it != post_handlers_.end()) {
        return it->second(conn);
    }
    std::cerr << "invalid post request path: " << path << "\n";
    return ErrorCode::NOT_FOUND;
}

void Dispatcher::register_post_handler(const std::string& path, HttpHandler handler) {
    post_handlers_[path] = std::move(handler);
}

void response_set_by_code(http::response<http::dynamic_body>& response, ErrorCode code) {
    if (code != ErrorCode::SUCCESS) {
        response.set(http::field::content_type, "text/plain");
    }

    switch (code) {
        case ErrorCode::SUCCESS:
            response.result(http::status::ok);
            break;
        case ErrorCode::FAILED:
        case ErrorCode::RPC_FAILED:
            response.result(http::status::internal_server_error);
            break;
        case ErrorCode::NOT_FOUND:
            response.result(http::status::not_found);
            boost::beast::ostream(response.body()) << "url not found\r\n";
            break;
        case ErrorCode::TIMEOUT:
            response.result(http::status::request_timeout);
            break;
        case ErrorCode::INVALID_JSON: {
            response.result(http::status::bad_request);
            response.set(http::field::content_type, "application/json");
            nlohmann::json error{{"status", static_cast<uint8_t>(ErrorCode::INVALID_JSON)},
                                 {"message", "invalid json"}};
            boost::beast::ostream(response.body()) << error.dump() << "\r\n";
            break;
        }
        case ErrorCode::UNKNOWN:
        default:
            response.result(http::status::bad_request);
            break;
    }
}