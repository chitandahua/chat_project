#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "mail_client.hpp"
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

        auto unique_id = boost::uuids::to_string(boost::uuids::random_generator()());
        std::string content = std::string("您的验证码为") + unique_id + "请三分钟内完成注册";
        // 用本机用户测试
        MailClient client("127.0.0.1", 25);
        client.send_mail("chitanda@localhost", "verify code", content);
        // client.send_mail(request->email(), "verify code", content);

        // 构造响应
        response->set_error(0);
        response->set_email(request->email());
        response->set_code("just test");

        return grpc::Status::OK;
    }

private:
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