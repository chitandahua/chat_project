#ifndef _GRPC_SERVICE_HPP_
#define _GRPC_SERVICE_HPP_

#include <grpcpp/grpcpp.h>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <cstddef>
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
                      std::shared_ptr<RedisClient>& redis_client,
                      std::vector<ServerConfig>& chat_servers)
        : ioc_(ioc), redis_client_(redis_client), chat_servers_(chat_servers) {}

    // 实现 GetChatServer RPC
    grpc::ServerUnaryReactor* GetChatServer(grpc::CallbackServerContext* context,
                                            const message::GetChatServerRequest* request,
                                            message::GetChatServerResponse* response) override {
        auto* reactor = context->DefaultReactor();

        boost::asio::co_spawn(
            *ioc_,
            [this, request, response, reactor]() -> boost::asio::awaitable<void> {
                response->set_error(0);
                auto index = co_await get_server_index();
                std::cout << "choose server index: " << index << "\n";

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
    boost::asio::awaitable<int> get_server_index() {
        // return ++server_index_ % chat_servers_.size();
        // 从Redis获取服务器login_count 选最小的一个
        using boost::redis::request;
        using boost::redis::response;

        // 默认选第一个
        int default_index = 0;
        auto result = co_await redis_client_->hgetall<std::string, int64_t>("login_count");
        if (result.empty()) {
            co_return default_index;
        }
        // 有可能server没更新login_count 初始化每个server的login_count为0
        std::map<std::string, std::pair<size_t, int64_t>> server_login_count;
        for (size_t i = 0; i < chat_servers_.size(); ++i) {
            auto& config = chat_servers_[i];
            server_login_count[config.name] = {i, 0};
        }
        for (auto& [key, value] : result) {
            // 不存在的server 则跳过
            if (auto it = server_login_count.find(key); it != server_login_count.end()) {
                it->second.second = value;
            }
        }

        // 找出map中 login_count 最小的一个
        int index = default_index;
        int64_t min_value = std::numeric_limits<int64_t>::max();
        for (auto& [key, value] : server_login_count) {
            std::cout << "server: " << key << " login_count: " << value.second << "\n";
            if (value.second < min_value) {
                min_value = value.second;
                index = value.first;
            }
        }

        co_return index;
    }

    std::atomic<uint64_t> server_index_{0};
    std::shared_ptr<boost::asio::io_context> ioc_;
    std::shared_ptr<RedisClient> redis_client_;
    std::vector<ServerConfig>& chat_servers_;
    std::mutex mutex_;
    std::map<int64_t, std::string> user_token_;
};

#endif