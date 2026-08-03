#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>

#include "message.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class VerifyServiceImpl final : public message::VerifyService::Service {
public:
    // 实现 GetVerifyCode RPC
    grpc::Status GetVerifyCode(grpc::ServerContext* context,
                               const message::GetVerifyRequest* request,
                               message::GetVerifyResponse* response) override {
        // 打印日志
        std::cout << "Received request for email: " << request->email() << std::endl;

        // 构造响应
        response->set_error(0);
        response->set_email(request->email());
        response->set_code(generate_verify_code());  // 生成6位随机验证码

        return grpc::Status::OK;
    }

private:
    std::string generate_verify_code() {
        // 简易随机验证码（6位数字）
        static const char digits[] = "0123456789";
        std::string code;
        code.resize(6);
        for (int i = 0; i < 6; ++i) {
            code[i] = digits[rand() % 10];
        }
        return code;
    }
};

int main(int argc, char** argv) {
    const std::string server_address = "127.0.0.1:10087";

    VerifyServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << "\n";

    server->Wait();

    return 0;
}