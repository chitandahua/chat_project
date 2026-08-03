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

#include "error_code.hpp"

using namespace boost::asio::ip;
using namespace boost::beast;

class Server;
class HttpConnection;

class Dispatcher {
public:
    using HttpHandler = std::function<ErrorCode(std::shared_ptr<HttpConnection>&)>;

    ErrorCode handle_get_request(std::shared_ptr<HttpConnection> conn, const std::string& path);
    void register_get_handler(const std::string& path, HttpHandler handler);
    ErrorCode handle_post_request(std::shared_ptr<HttpConnection> conn, const std::string& path);
    void register_post_handler(const std::string& path, HttpHandler handler);

    void init();

private:
    std::map<std::string, HttpHandler> get_handlers_;
    std::map<std::string, HttpHandler> post_handlers_;
};

void response_set_by_code(http::response<http::dynamic_body>& response, ErrorCode code);

#endif