#include <boost/url.hpp>
#include <memory>
#include <nlohmann/json.hpp>

#include "config.hpp"
#include "dispatcher.hpp"
#include "http_connection.hpp"

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
                {{"email", email}, {"code", rpc_result.value().email()}},
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

int Dispatcher::init(const ServerConfig& rpc_server_config) {
    auto target = rpc_server_config.host + ":" + std::to_string(rpc_server_config.port);
    channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    if (!channel_) {
        std::cerr << "Failed to create gRPC channel. Invalid address or port." << "\n";
        return -1;
    }

    grpc_client_ = std::make_shared<GrpcClient>();
    grpc_client_->verify_service_client_ = std::make_unique<VerifyServiceClient>(channel_);

    register_get_handler("/get_test", get_test);
    register_post_handler("/get_verify_code", [client = grpc_client_](auto conn) -> ErrorCode {
        return get_verify_code(conn, client);
    });

    return 0;
}

ErrorCode Dispatcher::handle_get_request(std::shared_ptr<HttpConnection> conn,
                                         const std::string& path) {
    auto it = get_handlers_.find(path);
    if (it != get_handlers_.end()) {
        return it->second(conn);
    }
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