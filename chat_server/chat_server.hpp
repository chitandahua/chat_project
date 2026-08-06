#ifndef _CHAT_SERVER_HPP_
#define _CHAT_SERVER_HPP_

#include <boost/asio.hpp>
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

class chat_participant {
public:
    virtual ~chat_participant() {}
    virtual void deliver(const MsgNode& msg) = 0;
};

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

class chat_room {
public:
    explicit chat_room(const std::string& server_name) : server_name_(server_name) {}

    void join(chat_participant_ptr participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        participants_.insert(participant);
    }

    void leave(chat_participant_ptr participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        participants_.erase(participant);
    }

    uint32_t participant_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return participants_.size();
    }

    std::string server_name_;

private:
    mutable std::mutex mutex_;
    std::set<chat_participant_ptr> participants_;
};

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

    void set_uid(int64_t uid) {
        uid_ = uid;
    }

    uint32_t server_session_count() {
        return room_.participant_count();
    }

    std::string server_name() {
        return room_.server_name_;
    }

private:
    awaitable<void> reader(const std::shared_ptr<MessageHandler>& handler) {
        try {
            for (MsgNode read_msg;;) {
                auto read_len = co_await asio::async_read(
                    socket_, boost::asio::buffer(read_msg.data(), MsgNode::PREFIX_LEN),
                    use_awaitable);
                if (read_len == 0) {
                    std::cout << "read len is 0, stop\n";
                    stop();
                    co_return;
                }

                if (!read_msg.decode_header()) {
                    stop();
                    co_return;
                }

                co_await asio::async_read(
                    socket_, boost::asio::buffer(read_msg.body(), read_msg.body_length()),
                    use_awaitable);

                deliver(co_await handler->handle_message(shared_from_this(), read_msg));
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
    int64_t uid_;
};

#endif