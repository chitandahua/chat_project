#ifndef _GRPC_SERVICE_HPP_
#define _GRPC_SERVICE_HPP_

#include <grpcpp/grpcpp.h>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <tl/expected.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "config.hpp"
#include "redis_client.hpp"

#include "message.grpc.pb.h"

class StatusServiceImpl final : public message::StatusService::CallbackService {
public:
    StatusServiceImpl(std::shared_ptr<boost::asio::io_context>& ioc,
                      std::vector<ServerConfig>& chat_servers)
        : ioc_(ioc), chat_servers_(chat_servers) {}

    // 实现 GetChatServer RPC
    grpc::ServerUnaryReactor* GetChatServer(grpc::CallbackServerContext* context,
                                            const message::GetChatServerRequest* request,
                                            message::GetChatServerResponse* response) override {
        auto* reactor = context->DefaultReactor();

        boost::asio::co_spawn(
            *ioc_,
            [this, response, reactor]() -> boost::asio::awaitable<void> {
                response->set_error(0);
                auto index = ++server_index_ % chat_servers_.size();
                response->set_host(chat_servers_[index].host);
                response->set_port(chat_servers_[index].port);
                response->set_token(boost::uuids::to_string(boost::uuids::random_generator()()));

                reactor->Finish(grpc::Status::OK);
                co_return;
            },
            boost::asio::detached);

        return reactor;
    }

private:
    std::atomic<uint64_t> server_index_{0};
    std::shared_ptr<boost::asio::io_context> ioc_;
    std::vector<ServerConfig>& chat_servers_;
};

#endif