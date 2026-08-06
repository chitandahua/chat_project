#include <boost/asio.hpp>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <set>

#include <boost/asio/experimental/concurrent_channel.hpp>

#include <boost/mysql/connection_pool.hpp>

#include "chat_server.hpp"
#include "config.hpp"
#include "handle_message.hpp"
#include "redis_client.hpp"

using namespace boost;

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

awaitable<void> listener(const std::string& name, tcp::acceptor acceptor,
                         std::shared_ptr<MessageHandler> handler) {
    chat_room room(name);

    for (;;) {
        std::make_shared<chat_session>(co_await acceptor.async_accept(use_awaitable), room)
            ->start(handler);
    }
}

int main(int argc, char* argv[]) {
    try {
        ChatConfig config;
        if (config.init("../config/chat_server.json") != 0) {
            return -1;
        }

        constexpr int threads_num = 4;
        asio::io_context io_context(threads_num);

        std::shared_ptr<boost::mysql::connection_pool> pool =
            std::make_shared<boost::mysql::connection_pool>(
                io_context,
                mysql::pool_params{
                    .server_address =
                        mysql::host_and_port{config.mysql.host,
                                             static_cast<unsigned short>(config.mysql.port)},
                    .username = config.mysql.user,
                    .password = config.mysql.pass,

                    .database = config.mysql.database,
                    .multi_queries = true,
                    .thread_safe = true,
                });
        pool->async_run(asio::detached);

        // redis
        auto redis_client = std::make_shared<RedisClient>();
        boost::asio::co_spawn(io_context, redis_client->run(config.redis),
                              [](std::exception_ptr p) {
                                  if (p)
                                      std::rethrow_exception(p);
                              });

        std::shared_ptr<MessageHandler> handler =
            std::make_shared<MessageHandler>(redis_client, pool);
        if (handler->init(config) != 0) {
            return -1;
        }

        co_spawn(io_context,
                 listener(config.server.name,
                          tcp::acceptor(io_context,
                                        boost::asio::ip::tcp::endpoint(
                                            boost::asio::ip::make_address(config.server.host),
                                            config.server.port)),
                          std::move(handler)),
                 detached);

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) {
            redis_client->conn_->cancel();
            io_context.stop();
        });

        std::vector<std::thread> threads;
        for (int i = 0; i < threads_num; ++i) {
            threads.emplace_back([&io_context] { io_context.run(); });
        }

        io_context.run();
        for (auto& t : threads) {
            t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}