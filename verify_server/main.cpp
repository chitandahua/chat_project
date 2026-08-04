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
#include "mail_client.hpp"
#include "message.grpc.pb.h"
#include "redis_client.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class VerifyServiceImpl final : public message::VerifyService::Service {
public:
    VerifyServiceImpl(std::shared_ptr<RedisClient>& client, boost::asio::io_context& ioc)
        : redis_client_(client), ioc_(ioc) {}

    // 先查redis 不存在则存到redis缓存中
    auto get_verify_code(const std::string& email)
        -> boost::asio::awaitable<tl::expected<std::string, int>> {
        constexpr std::string_view prefix = "code_";
        auto key = std::string(prefix) + email;

        using boost::redis::request;
        using boost::redis::response;

        boost::system::error_code ec;
        // GET
        request get_req;
        get_req.push("GET", key);
        response<std::optional<std::string>> get_resp;

        co_await redis_client_->conn_->async_exec(
            get_req, get_resp, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        // auto result = co_await redis_client_->conn_->async_try_get<std::string>(
        //     key, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec) {
            std::cerr << "redis try get error: " << ec.message() << std::endl;
            co_return tl::make_unexpected(-1);
        } else if (auto result = std::get<0>(get_resp).value()) {
            if (result) {
                std::cout << "get key: " << key << " value: " << result.value() << "\n";
                co_return result.value();
            } else {
                co_return tl::make_unexpected(-1);
            }
        }

        auto code = boost::uuids::to_string(boost::uuids::random_generator()());
        std::cout << "set key: " << key << " value: " << code << "\n";
        // SET
        request set_req;
        set_req.push("SETEX", key, "600", code);
        response<std::string> set_resp;
        co_await redis_client_->conn_->async_exec(
            set_req, set_resp, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec) {
            std::cerr << "redis set error: " << ec.message() << std::endl;
            co_return tl::make_unexpected(-1);
        }
        co_return code;
    }

    // 实现 GetVerifyCode RPC
    grpc::Status GetVerifyCode(grpc::ServerContext* context,
                               const message::GetVerifyRequest* request,
                               message::GetVerifyResponse* response) override {
        // 打印日志
        auto email = request->email();
        std::cout << "Received request for email: " << email << std::endl;

        // TODO
        auto unique_id =
            boost::asio::co_spawn(ioc_, this->get_verify_code(email), boost::asio::use_future)
                .get();
        if (!unique_id) {
            response->set_error(-1);
            response->set_email(email);
            return grpc::Status::OK;
        }

        std::string content =
            std::string("您的验证码为") + unique_id.value() + "请三分钟内完成注册";
        // 用本机用户测试
        MailClient mail_client("127.0.0.1", 25);
        // TODO
        // mail_client.send_mail("chitanda@localhost", "verify code", content);
        // mail_client.send_mail(request->email(), "verify code", content);

        // 构造响应
        response->set_error(0);
        response->set_email(email);
        response->set_code("just test");

        return grpc::Status::OK;
    }

private:
    std::shared_ptr<RedisClient> redis_client_;
    boost::asio::io_context& ioc_;
};

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