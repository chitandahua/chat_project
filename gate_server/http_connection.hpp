#ifndef _HTTP_CONNECTION_HPP_
#define _HTTP_CONNECTION_HPP_

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>

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

    boost::asio::awaitable<void> run();
    const std::string& get_uuid() {
        return uuid_;
    }

private:
    void clean_up();

    tcp::socket socket_;
    std::shared_ptr<Dispatcher> dispatcher_;
    std::weak_ptr<Server> server_;
    std::string uuid_;
};

#endif
