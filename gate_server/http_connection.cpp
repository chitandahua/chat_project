#include <boost/url.hpp>
#include <iostream>

#include "dispatcher.hpp"
#include "error.h"
#include "http_connection.hpp"
#include "server.hpp"

namespace asio = boost::asio;

HttpConnection::HttpConnection(tcp::socket sock, std::weak_ptr<Server> server,
                               std::shared_ptr<Dispatcher>& dispatcher)
    : socket_(std::move(sock)),
      dispatcher_(dispatcher),
      server_(std::move(server)),
      uuid_(boost::uuids::to_string(boost::uuids::random_generator()())) {}

HttpConnection::~HttpConnection() = default;

boost::asio::awaitable<void> HttpConnection::run() {
    using namespace std::chrono_literals;

    boost::system::error_code ec;
    boost::beast::flat_buffer buff;

    // A timer, to use with asio::cancel_after to implement timeouts.
    // Re-using the same timer multiple times with cancel_after
    // is more efficient than using raw cancel_after,
    // since the timer doesn't need to be re-created for every operation.
    boost::asio::steady_timer timer(co_await asio::this_coro::executor);

    // A HTTP session might involve more than one message if
    // keep-alive semantics are used. Loop until the connection closes.
    while (true) {
        http::request_parser<http::string_body> parser;

        parser.body_limit(10000);

        co_await http::async_read(socket_, buff, parser.get(),
                                  asio::cancel_after(timer, 60s, asio::redirect_error(ec)));

        if (ec) {
            if (ec == http::error::end_of_stream) {
                // This means they closed the connection
                socket_.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
            } else {
                // An unknown error happened
                log_error("Error reading HTTP request: ", ec);
            }
            clean_up();
            co_return;
        }

        const auto& request = parser.get();

        auto response = co_await asio::co_spawn(
            // Use the same executor as this coroutine (it will be a strand)
            co_await asio::this_coro::executor,

            [&] { return dispatcher_->handle_request(request); },

            // Completion token. Returns an object that can be co_await'ed
            asio::cancel_after(timer, 30s));

        // Adjust the response, setting fields common to all responses
        bool keep_alive = request.keep_alive();
        response.version(request.version());
        response.keep_alive(keep_alive);
        response.prepare_payload();

        co_await http::async_write(socket_, response,
                                   asio::cancel_after(timer, 60s, asio::redirect_error(ec)));
        if (ec) {
            log_error("Error writing HTTP response: ", ec);
            clean_up();
            co_return;
        }

        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        if (!keep_alive) {
            socket_.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
            clean_up();
            co_return;
        }
    }
}

void HttpConnection::clean_up() {
    std::cout << "clean connection\n";

    if (auto server = server_.lock()) {
        server->clear_session(get_uuid());
    }
}