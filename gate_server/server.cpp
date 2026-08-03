#include <iostream>

#include "config.hpp"
#include "context_pool.hpp"
#include "dispatcher.hpp"
#include "http_connection.hpp"
#include "server.hpp"

Server::Server(boost::asio::io_context& ioc, tcp::endpoint endpoint)
    : acceptor_(ioc, tcp::endpoint(std::move(endpoint))),
      context_pool_(std::make_unique<ContextPool>(4)),
      dispatcher_(std::make_shared<Dispatcher>()) {}

Server::~Server() {
    stop();
}

int Server::run(const ServerConfig& rpc_server_config) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return -1;
    }

    context_pool_->run();
    if (dispatcher_->init(rpc_server_config) != 0) {
        return -1;
    }
    start_accept();
    return 0;
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

void Server::start_accept() {
    auto sock = std::make_shared<tcp::socket>(context_pool_->get_io_context());
    acceptor_.async_accept(
        *sock, [self = shared_from_this(), sock](const boost::system::error_code& error) {
            if (!error) {
                std::cout << "accept new connection\n";
                auto session = std::make_shared<Session>(
                    std::move(*sock), std::weak_ptr<Server>(self), self->dispatcher_);
                self->add_session(session, session->get_uuid());
                session->start();
            } else {
                std::cerr << "accept error: " << error.message() << "\n";
            }
            self->start_accept();
        });
}