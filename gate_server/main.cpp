#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <iostream>
#include <memory>

#include "server.hpp"

int main() {
    try {
        boost::asio::io_context io_context;
        auto server = std::make_shared<Server>(io_context, 10086);
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&, server](const boost::system::error_code& error, int signal_number) {
            if (signal_number == SIGINT) {
                std::cout << "SIGINT received" << "\n";
            } else if (signal_number == SIGTERM) {
                std::cout << "SIGTERM received" << "\n";
            }
            server->stop();
            io_context.stop();
        });

        server->run();
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << e.what() << "\n";
    }

    return 0;
}