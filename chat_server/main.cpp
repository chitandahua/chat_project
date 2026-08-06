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
#include "grpc_service.hpp"
#include "handle_message.hpp"
#include "redis_client.hpp"

using namespace boost;

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

awaitable<void> listener(const std::shared_ptr<ChatServer>& chat_server, tcp::acceptor acceptor,
                         std::shared_ptr<MessageHandler> handler) {
    for (;;) {
        std::make_shared<ChatSession>(co_await acceptor.async_accept(use_awaitable), chat_server)
            ->start(handler);
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " <config file>" << std::endl;
            return -1;
        }

        ChatConfig config;
        if (config.init(argv[1]) != 0) {
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

        // redis 不能用多个线程共享的io_context跑！！！
        std::shared_ptr<asio::io_context> main_io_context = std::make_shared<asio::io_context>();
        auto redis_client = std::make_shared<RedisClient>();
        boost::asio::co_spawn(*main_io_context, redis_client->run(config.redis),
                              [](std::exception_ptr p) {
                                  if (p)
                                      std::rethrow_exception(p);
                              });

        std::shared_ptr<MessageHandler> handler =
            std::make_shared<MessageHandler>(redis_client, pool);
        if (handler->init(config) != 0) {
            return -1;
        }

        auto chat_server = std::make_shared<ChatServer>(config.server.name);
        co_spawn(*main_io_context,
                 listener(chat_server,
                          tcp::acceptor(*main_io_context,
                                        boost::asio::ip::tcp::endpoint(
                                            boost::asio::ip::make_address(config.server.host),
                                            config.server.port)),
                          std::move(handler)),
                 detached);

        // grpc server
        const std::string server_address =
            config.grpc_server.host + ":" + std::to_string(config.grpc_server.port);
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

        ChatServiceServer service(main_io_context, chat_server);
        builder.RegisterService(&service);

        std::shared_ptr<grpc::Server> server(builder.BuildAndStart());
        std::cout << "Grpc server listening on " << server_address << "\n";
        std::thread grpc_thread([=]() { server->Wait(); });

        asio::signal_set signals(*main_io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) {
            redis_client->conn_->cancel();
            server->Shutdown();
            io_context.stop();
            main_io_context->stop();
        });

        std::vector<std::thread> threads;
        for (int i = 0; i < threads_num; ++i) {
            threads.emplace_back([&io_context] { io_context.run(); });
        }

        main_io_context->run();
        if (grpc_thread.joinable()) {
            grpc_thread.join();
        }
        for (auto& t : threads) {
            t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}