#include <grpcpp/grpcpp.h>
#include <boost/asio/use_future.hpp>
#include <iostream>
#include <memory>
#include <string>

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <tl/expected.hpp>

#include "config.hpp"
#include "grpc_service.hpp"
#include "message.grpc.pb.h"
#include "redis_client.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::Status;

int main(int argc, char** argv) {
    try {
        // config
        VerifyConfig config;
        if (0 != config.init("../config/verify_server.json")) {
            return -1;
        }

        boost::asio::io_context ioc;
        auto redis_client = std::make_shared<RedisClient>();

        // grpc server
        const std::string server_address = "127.0.0.1:10087";
        ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

        VerifyServiceImpl service(redis_client, ioc);
        builder.RegisterService(&service);

        std::unique_ptr<Server> server(builder.BuildAndStart());
        std::cout << "Server listening on " << server_address << "\n";
        // 阻塞，但会在 io_context 的线程中执行
        // boost::asio::post(ioc, [server = std::move(server)] { server->Wait(); });
        std::thread grpc_thread([server = std::move(server)]() { server->Wait(); });

        // redis
        boost::asio::co_spawn(ioc, redis_client->run(config.redis), [](std::exception_ptr p) {
            if (p)
                std::rethrow_exception(p);
        });

        ioc.run();
        if (grpc_thread.joinable()) {
            grpc_thread.join();
        }

        return 0;
    } catch (std::exception const& e) {
        std::cerr << "(main) " << e.what() << "\n";
        return 1;
    }
}