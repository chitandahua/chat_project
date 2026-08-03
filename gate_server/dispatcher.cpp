#include <boost/url.hpp>

#include "dispatcher.hpp"
#include "http_connection.hpp"

void Dispatcher::init() {
    register_get_handler("/get_test", [](std::shared_ptr<HttpConnection>& connection) -> ErrorCode {
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
    });
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
    response.set(http::field::content_type, "text/plain");
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
        case ErrorCode::UNKNOWN:
        default:
            response.result(http::status::bad_request);
            break;
    }
}