#ifndef _SERVER_HPP_
#define _SERVER_HPP_

#include <boost/asio.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>

using namespace boost::asio::ip;
class HttpConnection;
class ContextPool;
class Dispatcher;
class Config;

class Server : public std::enable_shared_from_this<Server> {
public:
    using Session = HttpConnection;
    Server(boost::asio::io_context& ioc, tcp::endpoint endpoint);
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;
    ~Server();

    void add_session(std::shared_ptr<Session> session, const std::string& uuid) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[uuid] = std::move(session);
    }

    void clear_session(const std::string& uuid) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "clear session: " << uuid << "\n";
        sessions_.erase(uuid);
    }
    int run(const Config& config);
    void stop();

private:
    void start_accept();

    tcp::acceptor acceptor_;
    std::unique_ptr<ContextPool> context_pool_;
    std::atomic<bool> running_{false};
    std::shared_ptr<Dispatcher> dispatcher_;
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Session>> sessions_;
};

#endif
