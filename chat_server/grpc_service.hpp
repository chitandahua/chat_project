#ifndef _VERIFY_SERVICE_HPP_
#define _VERIFY_SERVICE_HPP_

#include <grpcpp/grpcpp.h>
#include <memory>
#include <tl/expected.hpp>

#include "message.grpc.pb.h"

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
        return response;
    }
};

#endif