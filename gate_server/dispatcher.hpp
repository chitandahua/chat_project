#ifndef _DISPATCHER_HPP_
#define _DISPATCHER_HPP_

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
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

#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>

#include "error.hpp"
#include "verify_service.hpp"

using namespace boost::asio::ip;

namespace asio = boost::asio;
namespace http = boost::beast::http;

class Server;
class HttpConnection;
class Config;
class VerifyServiceClient;
class MysqlConnPool;
class RequestData;

using RequestHandler =
    std::function<asio::awaitable<http::response<http::string_body>>(const RequestData&)>;

class HttpHandler {
public:
    HttpHandler(http::verb verb, RequestHandler&& request_handler) noexcept
        : method(verb), handler(std::move(request_handler)) {}

    http::verb method;
    RequestHandler handler;
};

class Dispatcher {
public:
    explicit Dispatcher(std::shared_ptr<boost::asio::io_context>& ioc);

    void register_get_handler(const std::string& path, RequestHandler&& handler);
    void register_post_handler(const std::string& path, RequestHandler&& handler);

    int init(const Config& config);
    asio::awaitable<http::response<http::string_body>> handle_request(
        const http::request<http::string_body>& request);

private:
    void handlers_init();

    std::shared_ptr<grpc::Channel> grpc_channel_;
    std::shared_ptr<grpc::Channel> status_grpc_channel_;
    std::shared_ptr<boost::redis::connection> redis_conn_;
    std::shared_ptr<MysqlConnPool> mysql_conn_;
    std::unordered_multimap<std::string_view, HttpHandler> handlers_;
};

#endif