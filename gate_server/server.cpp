#include <iostream>

#include "config.hpp"
#include "context_pool.hpp"
#include "dispatcher.hpp"
#include "error.h"
#include "http_connection.hpp"
#include "server.hpp"

Server::Server(std::shared_ptr<boost::asio::io_context>& ioc, tcp::endpoint endpoint)
    : acceptor_(*ioc, tcp::endpoint(std::move(endpoint))),
      context_pool_(std::make_unique<ContextPool>(4)),
      dispatcher_(std::make_shared<Dispatcher>(ioc)) {}

Server::~Server() {
    stop();
}

int Server::init(const Config& config) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return -1;
    }

    context_pool_->run();
    if (dispatcher_->init(config) != 0) {
        return -1;
    }

    return 0;
}

boost::asio::awaitable<void> Server::run() {
    while (true) {
        auto sock = std::make_shared<tcp::socket>(context_pool_->get_io_context());
        co_await acceptor_.async_accept(*sock);

        auto session = std::make_shared<Session>(
            std::move(*sock), std::weak_ptr<Server>(shared_from_this()), dispatcher_);
        add_session(session, session->get_uuid());

        asio::co_spawn(
            // Every session gets its own strand. This prevents data races.
            boost::asio::make_strand(co_await boost::asio::this_coro::executor),

            session->run(),

            [](std::exception_ptr ptr) {
                if (ptr) {
                    try {
                        std::rethrow_exception(ptr);
                    } catch (const std::exception& exc) {
                        auto guard = lock_cerr();
                        std::cerr << "Uncaught error in a session: " << exc.what() << std::endl;
                    }
                }
            });
    }
}

void Server::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // 防止重复调用 stop
    }

    acceptor_.close();
    context_pool_->stop();
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
}