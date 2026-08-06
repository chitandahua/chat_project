#ifndef _VERIFY_SERVICE_HPP_
#define _VERIFY_SERVICE_HPP_

#include <grpcpp/grpcpp.h>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <tl/expected.hpp>

#include "chat_server.hpp"
#include "message.grpc.pb.h"
#include "message_common.hpp"

class LoginMsgRequest {
public:
    int64_t uid;
    std::string token;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LoginMsgRequest, uid, token)
};

class LoginMsgResponse {
public:
    int64_t error;
    int64_t uid;
    std::string token;
    std::string name;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LoginMsgResponse, error, uid, token)
};

class LoginServiceClient {
public:
    // TODO 改成模板 4种类型 自定义类型请求实现operator XXType() 回复实现构造函数
    static tl::expected<message::LoginResponse, int> get_login_token(
        const std::shared_ptr<grpc::Channel>& channel, const LoginMsgRequest& login_msg) {
        message::LoginRequest request;
        grpc::ClientContext context;
        message::LoginResponse response;

        request.set_uid(login_msg.uid);
        request.set_token(login_msg.token);
        grpc::Status status =
            message::StatusService::NewStub(channel)->Login(&context, request, &response);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return tl::make_unexpected(-1);
        }
        std::cout << "get login token: " << response.token() << "\n";
        return response;
    }
};

class GrpcAddFriendRequest {
public:
    int64_t from_uid_;
    std::string name_;
    int64_t to_uid_;

    // TODO 改成AddFriendRequest和UserInfo构造
    GrpcAddFriendRequest(int64_t from_uid, const std::string& name, int64_t to_uid)
        : from_uid_(from_uid), name_(name), to_uid_(to_uid) {}

    explicit operator message::AddFriendReq() const {
        message::AddFriendReq request;
        request.set_applyuid(from_uid_);
        request.set_name(name_);
        request.set_touid(to_uid_);
        return request;
    }
};

class GrpcAddFriendResponse {
public:
    int64_t error;
    int64_t from_uid;
    int64_t to_uid;

    explicit GrpcAddFriendResponse(const message::AddFriendRsp& response)
        : error(response.error()), from_uid(response.applyuid()), to_uid(response.touid()) {}
};

class ChatServiceClient {
public:
    static tl::expected<GrpcAddFriendResponse, ServerError> notify_add_friend(
        const std::shared_ptr<grpc::Channel>& channel, GrpcAddFriendRequest&& req) {
        message::AddFriendReq request = static_cast<message::AddFriendReq>(req);
        grpc::ClientContext context;
        message::AddFriendRsp response;

        grpc::Status status =
            message::ChatService::NewStub(channel)->NotifyAddFriend(&context, request, &response);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return tl::make_unexpected(ServerError::RPCFailed);
        }
        return GrpcAddFriendResponse(response);
    }
};

class NotifyAddFriendRequest {
public:
    int64_t applyuid;  // 请求方uid
    std::string name;  // 请求方名字

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(NotifyAddFriendRequest, applyuid, name)
};

class ChatServiceServer final : public message::ChatService::CallbackService {
public:
    ChatServiceServer(std::shared_ptr<boost::asio::io_context>& ioc,
                      std::shared_ptr<ChatServer>& chat_server)
        : ioc_(ioc), chat_server_(chat_server) {}

    grpc::ServerUnaryReactor* NotifyAddFriend(grpc::CallbackServerContext* context,
                                              const message::AddFriendReq* request,
                                              message::AddFriendRsp* response) override {
        auto* reactor = context->DefaultReactor();

        boost::asio::co_spawn(
            *ioc_,
            [this, request, response, reactor]() -> boost::asio::awaitable<void> {
                std::shared_ptr<ChatSession> target_session =
                    std::dynamic_pointer_cast<ChatSession>(
                        chat_server_->get_participant(request->touid()));
                if (target_session) {
                    auto notify_msg = NotifyAddFriendRequest{request->touid(), request->name()};
                    target_session->deliver(
                        MsgNode(magic_enum::enum_integer(MessageId::NotifyAddFriend),
                                nlohmann::json(notify_msg).dump()));
                } else {
                    std::cout << "target session not found\n";
                }

                response->set_error(0);
                response->set_applyuid(request->applyuid());
                response->set_touid(request->touid());

                reactor->Finish(grpc::Status::OK);
                co_return;
            },
            boost::asio::detached);

        return reactor;
    }

private:
    std::shared_ptr<boost::asio::io_context> ioc_;
    std::shared_ptr<ChatServer> chat_server_;
};

#endif