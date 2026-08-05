#include <boost/asio.hpp>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <set>

#include <boost/asio/experimental/concurrent_channel.hpp>

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

    void start() {
        room_.join(shared_from_this());

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->reader(); },
            detached);

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->writer(); },
            detached);
    }

    void deliver(const MsgNode& msg) {
        bool ok = channel_.try_send(boost::system::error_code{}, std::make_shared<MsgNode>(msg));
        if (!ok) {
            // 至少打个日志,方便排查是不是消费跟不上生产
            std::cerr << "channel full, message dropped for session\n";
        }
    }

private:
    awaitable<void> reader() {
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

                deliver(read_msg);
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

awaitable<void> listener(tcp::acceptor acceptor) {
    chat_room room;

    for (;;) {
        std::make_shared<chat_session>(co_await acceptor.async_accept(use_awaitable), room)
            ->start();
    }
}

//----------------------------------------------------------------------

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: coroutines_server_v3 <port> [<port> ...]\n";
            return 1;
        }

        constexpr int threads_num = 4;
        asio::io_context io_context(threads_num);

        for (int i = 1; i < argc; ++i) {
            unsigned short port = std::atoi(argv[i]);
            co_spawn(io_context, listener(tcp::acceptor(io_context, {tcp::v4(), port})), detached);
        }

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