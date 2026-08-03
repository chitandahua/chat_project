#ifndef _DISPATCHER_HPP_
#define _DISPATCHER_HPP_

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http.hpp>

#include <map>
#include <memory>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <grpcpp/grpcpp.h>

#include "error_code.hpp"
#include "verify_service.hpp"

using namespace boost::asio::ip;
using namespace boost::beast;

class Server;
class HttpConnection;
class ServerConfig;
class VerifyServiceClient;

class Dispatcher {
public:
    using HttpHandler = std::function<ErrorCode(std::shared_ptr<HttpConnection>&)>;

    ErrorCode handle_get_request(std::shared_ptr<HttpConnection> conn, const std::string& path);
    void register_get_handler(const std::string& path, HttpHandler handler);
    ErrorCode handle_post_request(std::shared_ptr<HttpConnection> conn, const std::string& path);
    void register_post_handler(const std::string& path, HttpHandler handler);

    int init(const ServerConfig& rpc_server_config);

    // TODO 每加一个service都要加个字段 有没有多态或者其他方式 使用map保存之类的
    // 或者只能static 单例了
    class GrpcClient {
    public:
        std::shared_ptr<VerifyServiceClient> verify_service_client_;
    };

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::shared_ptr<GrpcClient> grpc_client_;
    std::map<std::string, HttpHandler> get_handlers_;
    std::map<std::string, HttpHandler> post_handlers_;
};

void response_set_by_code(http::response<http::dynamic_body>& response, ErrorCode code);

#endif