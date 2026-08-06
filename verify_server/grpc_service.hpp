#ifndef _GRPC_SERVICE_HPP_
#define _GRPC_SERVICE_HPP_

#include <grpcpp/grpcpp.h>
#include <tl/expected.hpp>

#include "mail_client.hpp"
#include "redis_client.hpp"

#include "message.grpc.pb.h"

class VerifyServiceImpl final : public message::VerifyService::CallbackService {
public:
    VerifyServiceImpl(std::shared_ptr<RedisClient>& client, boost::asio::io_context& ioc)
        : redis_client_(client), ioc_(ioc) {}

    // 先查redis 不存在则存到redis缓存中
    auto get_verify_code(const std::string& email)
        -> boost::asio::awaitable<tl::expected<std::string, int>> {
        // constexpr std::string_view prefix = "code_";
        auto key = email;

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
            std::cerr << "redis set error (transport/adapt): " << ec.message() << std::endl;
            co_return tl::make_unexpected(-1);
        }

        auto result = std::get<0>(set_resp);
        if (result.has_error()) {
            std::cerr << "redis set error (server): " << result.error().diagnostic << std::endl;
            co_return tl::make_unexpected(-1);
        }
        co_return code;
    }

    // 实现 GetVerifyCode RPC
    grpc::ServerUnaryReactor* GetVerifyCode(grpc::CallbackServerContext* context,
                                            const message::GetVerifyRequest* request,
                                            message::GetVerifyResponse* response) override {
        // 打印日志
        auto* reactor = context->DefaultReactor();
        auto email = request->email();

        // 把协程投递到 io_context,不在这里阻塞等待
        boost::asio::co_spawn(
            ioc_,
            [this, email, response, reactor]() -> boost::asio::awaitable<void> {
                auto unique_id = co_await this->get_verify_code(email);

                if (!unique_id) {
                    response->set_error(-1);
                    response->set_email(email);
                } else {
                    // ... 发邮件、设置正常响应 ...
                    std::string content =
                        std::string("您的验证码为") + unique_id.value() + "请三分钟内完成注册";
                    // TODO 用本机用户测试
                    // MailClient mail_client("127.0.0.1", 25);
                    // mail_client.send_mail("chitanda@localhost", "verify code", content);
                    // mail_client.send_mail(request->email(), "verify code", content);

                    response->set_error(0);
                    response->set_email(email);
                    response->set_code("just test");
                }

                // 结果准备好了,这时候才真正通知 gRPC 这次 RPC 完成
                reactor->Finish(grpc::Status::OK);
            },
            boost::asio::detached);

        // GetVerifyCode 这个函数立刻返回,不等结果
        return reactor;
    }

private:
    std::shared_ptr<RedisClient> redis_client_;
    boost::asio::io_context& ioc_;
};

#endif