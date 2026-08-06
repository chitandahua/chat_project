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

class ChatParticipant {
public:
    virtual ~ChatParticipant() {}
    virtual void deliver(const MsgNode& msg) = 0;
    virtual void deliver(MsgNode&& msg) = 0;
    virtual int64_t uid() = 0;
};

typedef std::shared_ptr<ChatParticipant> ChatParticipantPtr;

class ChatServer : std::enable_shared_from_this<ChatServer> {
public:
    explicit ChatServer(const std::string& server_name) : server_name_(server_name) {}

    void join(ChatParticipantPtr participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        participants_.insert(participant);
    }

    // TODO logout/断链接 更新redis登录数？
    void leave(ChatParticipantPtr participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        participants_.erase(participant);
    }

    uint32_t participant_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return participants_.size();
    }

    const std::string& name() const {
        return server_name_;
    }

    std::shared_ptr<ChatParticipant> get_participant(int64_t uid) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& participant : participants_) {
            if (participant->uid() == uid) {
                return participant;
            }
        }
        return nullptr;
    }

private:
    std::string server_name_;
    mutable std::mutex mutex_;
    std::set<ChatParticipantPtr> participants_;
};

class ChatSession : public ChatParticipant, public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(tcp::socket socket, const std::shared_ptr<ChatServer>& server)
        : socket_(std::move(socket)), server_(server), channel_(socket_.get_executor(), 64) {}

    void start(std::shared_ptr<MessageHandler>& handler) {
        if (auto server = server_.lock()) {
            server->join(shared_from_this());
        }

        co_spawn(
            socket_.get_executor(),
            [self = shared_from_this(), handler = handler] { return self->reader(handler); },
            detached);

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->writer(); },
            detached);
    }

    void deliver(const MsgNode& msg) override {
        std::cout << "deliver msg id=" << msg.id() << " length=" << msg.length()
                  << " body=" << msg.body() << "\n";
        bool ok = channel_.try_send(boost::system::error_code{}, std::make_shared<MsgNode>(msg));
        if (!ok) {
            // 至少打个日志,方便排查是不是消费跟不上生产
            std::cerr << "channel full, message dropped for session\n";
        }
    }

    void deliver(MsgNode&& msg) override {
        bool ok = channel_.try_send(boost::system::error_code{},
                                    std::make_shared<MsgNode>(std::move(msg)));
        if (!ok) {
            // 至少打个日志,方便排查是不是消费跟不上生产
            std::cerr << "channel full, message dropped for session\n";
        }
    }

    void set_uid(int64_t uid) {
        uid_ = uid;
    }

    virtual int64_t uid() override {
        return uid_;
    }

    std::shared_ptr<ChatServer> server() {
        return server_.lock();
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
        if (auto server = server_.lock()) {
            server->leave(shared_from_this());
        }
        socket_.close();
    }

    tcp::socket socket_;
    std::weak_ptr<ChatServer> server_;
    asio::experimental::concurrent_channel<void(boost::system::error_code,
                                                std::shared_ptr<MsgNode>)>
        channel_;
    int64_t uid_;
};

#endif