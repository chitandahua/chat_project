#include <boost/url.hpp>
#include <iostream>

#include "dispatcher.hpp"
#include "http_connection.hpp"
#include "server.hpp"

HttpConnection::HttpConnection(tcp::socket sock, std::weak_ptr<Server> server,
                               std::shared_ptr<Dispatcher>& dispatcher)
    : socket_(std::move(sock)),
      server_(std::move(server)),
      uuid_(boost::uuids::to_string(boost::uuids::random_generator()())),
      deadline_(socket_.get_executor(), std::chrono::seconds(60)),
      dispatcher_(dispatcher) {}

HttpConnection::~HttpConnection() = default;

void HttpConnection::start() {
    http::async_read(socket_, buffer_, request_,
                     [self = shared_from_this()](const boost::system::error_code& ec,
                                                 std::size_t bytes_transferred) {
                         if (!ec) {
                             boost::ignore_unused(bytes_transferred);
                             self->handle_request();
                             self->check_deadline();
                         } else {
                             // TODO 判断错误码等
                             if (ec != boost::asio::error::eof) {
                                 std::cout << "Invalid http request: " << ec.message() << "\n";
                             }
                             if (auto server = self->server_.lock()) {
                                 server->clear_session(self->get_uuid());
                             }
                         }
                     });
}

void HttpConnection::handle_request() {
    response_.version(request_.version());
    // 短连接
    response_.keep_alive(false);

    ErrorCode result = ErrorCode::FAILED;
    auto uri = boost::urls::parse_origin_form(request_.target());
    if (uri) {
        if (request_.method() == http::verb::get) {
            result = dispatcher_->handle_get_request(shared_from_this(), uri.value().path());
        } else if (request_.method() == http::verb::post) {
            result = dispatcher_->handle_post_request(shared_from_this(), uri.value().path());
        }
    }
    response_set_by_code(response_, result);
    write_response();
}

void HttpConnection::write_response() {
    response_.content_length(response_.body().size());
    http::async_write(socket_, response_,
                      [self = shared_from_this()](const boost::system::error_code& ec,
                                                  std::size_t bytes_transferred) {
                          if (!ec) {
                              self->socket_.shutdown(tcp::socket::shutdown_send);
                              self->deadline_.cancel();
                          } else {
                              std::cout << "write error: " << ec.message() << "\n";
                              if (auto server = self->server_.lock()) {
                                  server->clear_session(self->get_uuid());
                              }
                          }
                      });
}

void HttpConnection::check_deadline() {
    deadline_.async_wait([self = shared_from_this()](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            std::cout << "deadline error: " << ec.message() << "\n";
        } else {
            self->socket_.close();
        }
    });
}