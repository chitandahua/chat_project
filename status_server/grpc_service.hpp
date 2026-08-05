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

enum class GrpcErrorCode : uint8_t {
    SUCCESS = 0,
    INVALID_UID_OR_TOKEN = 1,
};

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
            [this, request, response, reactor]() -> boost::asio::awaitable<void> {
                response->set_error(0);
                auto index = ++server_index_ % chat_servers_.size();
                response->set_host(chat_servers_[index].host);
                response->set_port(chat_servers_[index].port);
                // TODO 超时清理 or 直接保存到redis中
                auto token = boost::uuids::to_string(boost::uuids::random_generator()());
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    user_token_[request->uid()] = token;
                }
                response->set_token(boost::uuids::to_string(boost::uuids::random_generator()()));

                reactor->Finish(grpc::Status::OK);
                co_return;
            },
            boost::asio::detached);

        return reactor;
    }

    grpc::ServerUnaryReactor* Login(grpc::CallbackServerContext* context,
                                    const message::LoginRequest* request,
                                    message::LoginResponse* response) override {
        auto* reactor = context->DefaultReactor();

        boost::asio::co_spawn(
            *ioc_,
            [this, request, response, reactor]() -> boost::asio::awaitable<void> {
                std::string token;
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = user_token_.find(request->uid());
                    if (it != user_token_.end()) {
                        token = it->second;
                        found = true;
                    }
                }

                if (!found) {
                    response->set_error(static_cast<int>(GrpcErrorCode::INVALID_UID_OR_TOKEN));
                } else {
                    response->set_error(static_cast<int>(GrpcErrorCode::SUCCESS));
                    response->set_uid(request->uid());
                    response->set_token(token);
                }

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
    std::mutex mutex_;
    std::map<int64_t, std::string> user_token_;
};

#endif