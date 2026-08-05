#include <boost/asio.hpp>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <set>

#include <boost/asio/experimental/concurrent_channel.hpp>

#include <boost/mysql/connection_pool.hpp>

#include "config.hpp"
#include "handle_message.hpp"
#include "msg_node.hpp"

using namespace boost;

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

//----------------------------------------------------------------------

class chat_participant {
public:
    virtual ~chat_participant() {}
    virtual void deliver(const MsgNode& msg) = 0;
};

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

class chat_room {
public:
    void join(chat_participant_ptr participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        participants_.insert(participant);
    }

    void leave(chat_participant_ptr participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        participants_.erase(participant);
    }

private:
    std::mutex mutex_;
    std::set<chat_participant_ptr> participants_;
};

//----------------------------------------------------------------------

class chat_session : public chat_participant, public std::enable_shared_from_this<chat_session> {
public:
    chat_session(tcp::socket socket, chat_room& room)
        : socket_(std::move(socket)), room_(room), channel_(socket_.get_executor(), 64) {}

    void start(std::shared_ptr<MessageHandler>& handler) {
        room_.join(shared_from_this());

        co_spawn(
            socket_.get_executor(),
            [self = shared_from_this(), handler = handler] { return self->reader(handler); },
            detached);

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->writer(); },
            detached);
    }

    void deliver(const MsgNode& msg) {
        std::cout << "deliver msg id=" << msg.id() << " length=" << msg.length()
                  << " body=" << msg.body() << "\n";
        bool ok = channel_.try_send(boost::system::error_code{}, std::make_shared<MsgNode>(msg));
        if (!ok) {
            // 至少打个日志,方便排查是不是消费跟不上生产
            std::cerr << "channel full, message dropped for session\n";
        }
    }

private:
    awaitable<void> reader(const std::shared_ptr<MessageHandler>& handler) {
        try {
            for (MsgNode read_msg;;) {
                co_await asio::async_read(socket_,
                                          boost::asio::buffer(read_msg.data(), MsgNode::PREFIX_LEN),
                                          use_awaitable);
                if (!read_msg.decode_header()) {
                    stop();
                    co_return;
                }

                co_await asio::async_read(
                    socket_, boost::asio::buffer(read_msg.body(), read_msg.body_length()),
                    use_awaitable);

                deliver(co_await handler->handle_message(read_msg));
            }
        } catch (std::exception&) {
            stop();
        }
    }

    awaitable<void> writer() {
        try {
            for (;;) {
                auto msg = co_await channel_.async_receive(use_awaitable);
                co_await asio::async_write(socket_, asio::buffer(msg->data(), msg->length()),
                                           use_awaitable);
            }
        } catch (std::exception&) {
            stop();
        }
    }

    void stop() {
        room_.leave(shared_from_this());
        socket_.close();
    }

    tcp::socket socket_;
    chat_room& room_;
    asio::experimental::concurrent_channel<void(boost::system::error_code,
                                                std::shared_ptr<MsgNode>)>
        channel_;
};

//----------------------------------------------------------------------

awaitable<void> listener(tcp::acceptor acceptor, std::shared_ptr<MessageHandler> handler) {
    chat_room room;

    for (;;) {
        std::make_shared<chat_session>(co_await acceptor.async_accept(use_awaitable), room)
            ->start(handler);
    }
}

//----------------------------------------------------------------------

int main(int argc, char* argv[]) {
    try {
        ChatConfig config;
        if (config.init("../config/chat_server.json") != 0) {
            return -1;
        }

        constexpr int threads_num = 4;
        asio::io_context io_context(threads_num);

        std::shared_ptr<boost::mysql::connection_pool> pool =
            std::make_shared<boost::mysql::connection_pool>(
                io_context,
                mysql::pool_params{
                    .server_address =
                        mysql::host_and_port{config.mysql.host,
                                             static_cast<unsigned short>(config.mysql.port)},
                    .username = config.mysql.user,
                    .password = config.mysql.pass,

                    .database = config.mysql.database,
                    .multi_queries = true,
                    .thread_safe = true,
                });
        pool->async_run(asio::detached);

        std::shared_ptr<MessageHandler> handler = std::make_shared<MessageHandler>(pool);
        if (handler->init(config) != 0) {
            return -1;
        }

        co_spawn(io_context,
                 listener(tcp::acceptor(io_context,
                                        boost::asio::ip::tcp::endpoint(
                                            boost::asio::ip::make_address(config.server.host),
                                            config.server.port)),
                          std::move(handler)),
                 detached);

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) { io_context.stop(); });

        std::vector<std::thread> threads;
        for (int i = 0; i < threads_num; ++i) {
            threads.emplace_back([&io_context] { io_context.run(); });
        }

        io_context.run();
        for (auto& t : threads) {
            t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}