#include <boost/url.hpp>
#include <nlohmann/json.hpp>

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

ErrorCode get_verify_code(std::shared_ptr<HttpConnection>& connection) {
    auto body = boost::beast::buffers_to_string(connection->request().body().data());
    std::cout << "body: " << body << "\n";

    try {
        auto json = nlohmann::json::parse(body);
        auto email = json["email"];
        nlohmann::json result{
            "status",
            static_cast<uint8_t>(ErrorCode::SUCCESS),
            "data",
            {{"email", email}},
        };

        connection->response().set(http::field::content_type, "application/json");
        boost::beast::ostream(connection->response().body()) << result.dump() << "\r\n";
    } catch (const nlohmann::json::parse_error& e) {
        std::cout << "parse error: " << e.what() << "\n";
        return ErrorCode::INVALID_JSON;
    }

    return ErrorCode::SUCCESS;
}

void Dispatcher::init() {
    register_get_handler("/get_test", get_test);
    register_post_handler("/get_verify_code", get_verify_code);
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
        case ErrorCode::RPC_FAILED:
        case ErrorCode::UNKNOWN:
        default:
            response.result(http::status::bad_request);
            break;
    }
}