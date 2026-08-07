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

class GrpcAuthFriendRequest {
public:
    int64_t from_uid;
    int64_t to_uid;

    explicit operator message::AuthFriendReq() const {
        message::AuthFriendReq request;
        request.set_fromuid(from_uid);
        request.set_touid(to_uid);
        return request;
    }
};

class GrpcAuthFriendResponse {
public:
    int64_t error;
    int64_t from_uid;
    int64_t to_uid;

    explicit GrpcAuthFriendResponse(const message::AuthFriendRsp& response)
        : error(response.error()), from_uid(response.fromuid()), to_uid(response.touid()) {}
};

class TextChatData {
public:
    int64_t msgid;
    std::string content;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TextChatData, msgid, content)
};

class GrpcTextChatMsgRequest {
public:
    int64_t from_uid;
    int64_t to_uid;
    std::vector<TextChatData> text_array;

    explicit operator message::TextChatMsgReq() const {
        message::TextChatMsgReq request;
        request.set_fromuid(from_uid);
        request.set_touid(to_uid);
        for (const auto& text : text_array) {
            auto* msg = request.add_textmsgs();
            msg->set_msg_id(text.msgid);
            msg->set_msgcontent(text.content);
        }
        return request;
    }
};

class GrpcTextChatMsgResponse {
public:
    int64_t error;
    int64_t from_uid;
    int64_t to_uid;
    std::vector<TextChatData> text_array;

    explicit GrpcTextChatMsgResponse(const message::TextChatMsgRsp& response)
        : error(response.error()), from_uid(response.fromuid()), to_uid(response.touid()) {
        for (const auto& msg : response.textmsgs()) {
            text_array.emplace_back(TextChatData{msg.msg_id(), msg.msgcontent()});
        }
    }
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

    static tl::expected<GrpcAuthFriendResponse, ServerError> notify_auth_friend(
        const std::shared_ptr<grpc::Channel>& channel, GrpcAuthFriendRequest&& req) {
        message::AuthFriendReq request = static_cast<message::AuthFriendReq>(req);
        grpc::ClientContext context;
        message::AuthFriendRsp response;

        grpc::Status status =
            message::ChatService::NewStub(channel)->NotifyAuthFriend(&context, request, &response);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return tl::make_unexpected(ServerError::RPCFailed);
        }
        return GrpcAuthFriendResponse(response);
    }

    static tl::expected<GrpcTextChatMsgResponse, ServerError> notify_text_chat_msg(
        const std::shared_ptr<grpc::Channel>& channel, GrpcTextChatMsgRequest&& req) {
        message::TextChatMsgReq request = static_cast<message::TextChatMsgReq>(req);
        grpc::ClientContext context;
        message::TextChatMsgRsp response;

        grpc::Status status =
            message::ChatService::NewStub(channel)->NotifyTextChatMsg(&context, request, &response);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return tl::make_unexpected(ServerError::RPCFailed);
        }
        return GrpcTextChatMsgResponse(response);
    }
};

// 用于chat session通信
class NotifyAddFriendMsg {
public:
    int64_t applyuid = 0;  // 请求方uid
    std::string name;      // 请求方名字

    NotifyAddFriendMsg() = default;
    explicit NotifyAddFriendMsg(const message::AddFriendReq& request)
        : applyuid(request.applyuid()), name(request.name()) {}
    NotifyAddFriendMsg(int64_t uid, const std::string& username) : applyuid(uid), name(username) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(NotifyAddFriendMsg, applyuid, name)
};

class NotifyAuthFriendMsg {
public:
    int64_t fromuid = 0;  // 认证方uid
    int64_t touid = 0;    // 申请方uid

    NotifyAuthFriendMsg() = default;
    explicit NotifyAuthFriendMsg(const message::AuthFriendReq& request)
        : fromuid(request.fromuid()), touid(request.touid()) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(NotifyAuthFriendMsg, fromuid, touid)
};

class NotifyTextChatMessage {
public:
    int64_t fromuid = 0;
    int64_t touid = 0;
    std::vector<TextChatData> text_array;

    NotifyTextChatMessage() = default;
    explicit NotifyTextChatMessage(const message::TextChatMsgReq& request)
        : fromuid(request.fromuid()), touid(request.touid()) {
        for (const auto& msg : request.textmsgs()) {
            text_array.emplace_back(TextChatData{msg.msg_id(), msg.msgcontent()});
        }
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(NotifyTextChatMessage, fromuid, touid, text_array)
};

class ChatServiceServer final : public message::ChatService::CallbackService {
    template <typename T, typename V>
    void notify_session(const T* request, int64_t uid, MessageId id) {
        std::shared_ptr<ChatSession> target_session =
            std::dynamic_pointer_cast<ChatSession>(chat_server_->get_participant(uid));
        if (target_session) {
            auto notify_msg = V(*request);
            target_session->deliver(
                MsgNode(magic_enum::enum_integer(id), nlohmann::json(notify_msg).dump()));
        } else {
            std::cout << "target session not found\n";
        }
    }

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
                notify_session<message::AddFriendReq, NotifyAddFriendMsg>(
                    request, request->touid(), MessageId::NotifyAddFriend);

                response->set_error(0);
                response->set_applyuid(request->applyuid());
                response->set_touid(request->touid());

                reactor->Finish(grpc::Status::OK);
                co_return;
            },
            boost::asio::detached);

        return reactor;
    }

    grpc::ServerUnaryReactor* NotifyAuthFriend(grpc::CallbackServerContext* context,
                                               const message::AuthFriendReq* request,
                                               message::AuthFriendRsp* response) override {
        auto* reactor = context->DefaultReactor();

        boost::asio::co_spawn(
            *ioc_,
            [this, request, response, reactor]() -> boost::asio::awaitable<void> {
                notify_session<message::AuthFriendReq, NotifyAuthFriendMsg>(
                    request, request->touid(), MessageId::NotifyAuthFriend);

                response->set_error(0);
                response->set_fromuid(request->fromuid());
                response->set_touid(request->touid());

                reactor->Finish(grpc::Status::OK);
                co_return;
            },
            boost::asio::detached);

        return reactor;
    }

    grpc::ServerUnaryReactor* NotifyTextChatMsg(grpc::CallbackServerContext* context,
                                                const message::TextChatMsgReq* request,
                                                message::TextChatMsgRsp* response) override {
        auto* reactor = context->DefaultReactor();

        boost::asio::co_spawn(
            *ioc_,
            [this, request, response, reactor]() -> boost::asio::awaitable<void> {
                notify_session<message::TextChatMsgReq, NotifyTextChatMessage>(
                    request, request->touid(), MessageId::NotifyTextChatMsg);

                response->set_error(0);

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