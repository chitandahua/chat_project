#ifndef _DISPATCHER_HPP_
#define _DISPATCHER_HPP_

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http.hpp>

#include <map>
#include <memory>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <grpcpp/grpcpp.h>
#include <boost/redis/connection.hpp>

#include "error_code.hpp"
#include "verify_service.hpp"

using namespace boost::asio::ip;
using namespace boost::beast;

class Server;
class HttpConnection;
class Config;
class VerifyServiceClient;
class MysqlConnPool;

class Dispatcher {
public:
    using HttpHandler = std::function<ErrorCode(std::shared_ptr<HttpConnection>&)>;
    explicit Dispatcher(boost::asio::io_context& ioc);

    ErrorCode handle_get_request(std::shared_ptr<HttpConnection> conn, const std::string& path);
    void register_get_handler(const std::string& path, HttpHandler handler);
    ErrorCode handle_post_request(std::shared_ptr<HttpConnection> conn, const std::string& path);
    void register_post_handler(const std::string& path, HttpHandler handler);

    int init(const Config& config);

    // TODO 每加一个service都要加个字段 有没有多态或者其他方式 使用map保存之类的
    // stub其实没必要保存 每次调用时再构造也不迟
    class GrpcClient {
    public:
        std::shared_ptr<VerifyServiceClient> verify_service_client_;
    };

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::shared_ptr<GrpcClient> grpc_client_;
    std::shared_ptr<boost::redis::connection> redis_conn_;
    std::shared_ptr<MysqlConnPool> mysql_conn_;
    std::map<std::string, HttpHandler> get_handlers_;
    std::map<std::string, HttpHandler> post_handlers_;
};

void response_set_by_code(http::response<http::dynamic_body>& response, ErrorCode code);

#endif