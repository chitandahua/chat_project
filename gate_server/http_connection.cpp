#include <boost/url.hpp>
#include <iostream>

#include "dispatcher.hpp"
#include "http_connection.hpp"
#include "server.hpp"

HttpConnection::HttpConnection(tcp::socket sock, std::weak_ptr<Server> server,
                               std::shared_ptr<Dispatcher>& dispatcher)
    : socket_(std::move(sock)),
      deadline_(socket_.get_executor(), std::chrono::seconds(60)),
      dispatcher_(dispatcher),
      server_(std::move(server)),
      uuid_(boost::uuids::to_string(boost::uuids::random_generator()())) {}

HttpConnection::~HttpConnection() = default;

void HttpConnection::start() {
    read_request();
    check_deadline();
}

void HttpConnection::read_request() {
    http::async_read(socket_, buffer_, request_,
                     [self = shared_from_this()](const boost::system::error_code& ec, std::size_t) {
                         if (!ec) {
                             self->handle_request();
                         } else {
                             // TODO 判断错误码等
                             if (ec != boost::asio::error::eof) {
                                 std::cout << "read error: " << ec.message() << "\n";
                             }
                             self->clean_up();
                         }

                         // TODO 长连接
                         // if (self->request_.keep_alive()) {
                         //     // 继续读下一个请求
                         //     self->read_request();
                         // }
                     });
}

void HttpConnection::handle_request() {
    std::cout << "receive request: " << request_.target() << "\n";

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
        } else {
            std::cerr << "invalid request method: " << request_.method() << "\n";
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
                              boost::system::error_code ignore_ec;
                              self->socket_.shutdown(tcp::socket::shutdown_send, ignore_ec);
                          } else {
                              std::cout << "write error: " << ec.message() << "\n";
                          }
                          self->clean_up();
                      });
}

void HttpConnection::check_deadline() {
    deadline_.async_wait([self = shared_from_this()](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;  // cancel 说明触发了clean_up(无论读写哪个触发的) 然后会很快触发析构了
                     // socket_也会close掉
        }
        if (ec) {
            std::cout << "deadline error: " << ec.message() << "\n";
        }

        std::cout << "connection[" << self->get_uuid() << "] timeout\n";
        // 超时关闭 关闭一个 socket,会强制取消这个 socket 上所有还挂起的异步操作
        // :调用 check_deadline() 意味着当前一定还有别的异步操作正在这个连接上挂着(要么
        // read_request() 的 async_read 还在等客户端把请求发完整,要么 write_response() 的
        // async_write 还在等数据发出去)。close() 一执行 就会触发读/写里面的 clean_up()
        self->socket_.close();
    });
}

void HttpConnection::clean_up() {
    std::cout << "clean connection\n";
    deadline_.cancel();
    if (auto server = server_.lock()) {
        server->clear_session(get_uuid());
    }
}