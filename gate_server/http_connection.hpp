#ifndef _HTTP_CONNECTION_HPP_
#define _HTTP_CONNECTION_HPP_

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace boost::asio::ip;
using namespace boost::beast;
class Server;
class Dispatcher;

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(tcp::socket sock, std::weak_ptr<Server> server,
                   std::shared_ptr<Dispatcher>& dispatcher);
    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;
    HttpConnection(HttpConnection&&) noexcept = default;
    HttpConnection& operator=(HttpConnection&&) noexcept = default;
    ~HttpConnection();

    void start();
    const std::string& get_uuid() {
        return uuid_;
    }

    auto& response() {
        return response_;
    }

    const auto& response() const {
        return response_;
    }

    auto& request() {
        return request_;
    }

    const auto& request() const {
        return request_;
    }

private:
    void check_deadline();
    void write_response();
    void handle_request();

    tcp::socket socket_;
    boost::beast::flat_buffer buffer_{8192};
    http::request<http::dynamic_body> request_;
    http::response<http::dynamic_body> response_;
    boost::asio::steady_timer deadline_;
    std::shared_ptr<Dispatcher> dispatcher_;
    std::weak_ptr<Server> server_;
    std::string uuid_;
};

#endif
