#ifndef _VERIFY_SERVICE_HPP_
#define _VERIFY_SERVICE_HPP_

#include <grpcpp/grpcpp.h>
#include <memory>
#include <tl/expected.hpp>

#include "error.hpp"
#include "message.grpc.pb.h"

class VerifyServiceClient {
public:
    VerifyServiceClient(const std::shared_ptr<grpc::Channel>& channel)
        : verify_service_stub_(message::VerifyService::NewStub(channel)) {}

    tl::expected<message::GetVerifyResponse, std::string> get_verify_code(
        const std::string& email) {
        message::GetVerifyRequest request;
        grpc::ClientContext context;
        message::GetVerifyResponse response;

        request.set_email(email);
        grpc::Status status = verify_service_stub_->GetVerifyCode(&context, request, &response);
        if (!status.ok()) {
            return tl::make_unexpected(status.error_message());
        }
        return response;
    }

private:
    std::unique_ptr<message::VerifyService::Stub> verify_service_stub_;
};

class StatusServiceClient {
public:
    static tl::expected<message::GetChatServerResponse, ServiceError> get_chat_server(
        const std::shared_ptr<grpc::Channel>& channel, int64_t id) {
        message::GetChatServerRequest request;
        grpc::ClientContext context;
        message::GetChatServerResponse response;

        request.set_uid(id);
        grpc::Status status =
            message::StatusService::NewStub(channel)->GetChatServer(&context, request, &response);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return tl::make_unexpected(ServiceError::RPC_FAILED);
        }
        return response;
    }
};

#endif