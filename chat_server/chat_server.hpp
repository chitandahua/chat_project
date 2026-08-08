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
    virtual void deliver(const ChannelMessage& msg) = 0;
    virtual void deliver(ChannelMessage&& msg) = 0;
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
        for (const auto& participant : participants_) {
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
        : socket_(std::move(socket)),
          server_(server),
          channel_(socket_.get_executor(), 64),
          login_deadline_(socket_.get_executor()) {}

    void start(std::shared_ptr<MessageHandler>& handler) {
        if (auto server = server_.lock()) {
            server->join(shared_from_this());
        }

        // 未登录状态的超时保护:10 秒内必须完成登录,否则强制断开
        login_deadline_.expires_after(std::chrono::seconds(10));
        co_spawn(
            socket_.get_executor(),
            [self = shared_from_this()] { return self->check_login_deadline(); }, detached);

        co_spawn(
            socket_.get_executor(),
            [self = shared_from_this(), handler = handler] { return self->reader(handler); },
            detached);

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->writer(); },
            detached);
    }

    void deliver(MsgNode&& msg) {
        deliver(ChannelMessage(std::move(msg)));
    }

    void deliver(const ChannelMessage& msg) override {
        std::cout << "deliver msg id=" << msg.msg->id() << " length=" << msg.msg->length()
                  << " body=" << msg.msg->body() << "\n";
        bool ok = channel_.try_send(boost::system::error_code{}, msg);
        if (!ok) {
            // 至少打个日志,方便排查是不是消费跟不上生产
            std::cerr << "channel full, message dropped for session\n";
        }
    }

    void deliver(ChannelMessage&& msg) override {
        bool ok = channel_.try_send(boost::system::error_code{}, std::move(msg));
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

    bool is_authenticated() const {
        return is_authenticated_;
    }

    // 登录成功时调用:标记已登录 + 取消登录超时保护
    void mark_authenticated() {
        is_authenticated_ = true;
        login_deadline_.cancel();
    }

private:
    awaitable<void> check_login_deadline() {
        boost::system::error_code ec;
        co_await login_deadline_.async_wait(redirect_error(use_awaitable, ec));
        if (ec == boost::asio::error::operation_aborted) {
            co_return;  // 被 cancel() 打断,说明登录成功了(或者连接已经在别处被关闭),不用处理
        }
        // 真超时,还没登录,强制断开
        if (!is_authenticated_) {
            std::cerr << "session " << (void*)this << " login timeout, closing\n";
            stop();
        }
    }

    awaitable<void> reader(const std::shared_ptr<MessageHandler>& handler) {
        try {
            for (;;) {
                MsgNode read_msg;
                auto read_len = co_await asio::async_read(
                    socket_, boost::asio::buffer(read_msg.data(), MsgNode::PREFIX_LEN),
                    use_awaitable);

                if (!read_msg.decode_header()) {
                    stop();
                    co_return;
                }

                co_await asio::async_read(
                    socket_, boost::asio::buffer(read_msg.body(), read_msg.body_length()),
                    use_awaitable);

                deliver(co_await handler->handle_message(shared_from_this(), read_msg));
            }
        } catch (const boost::system::system_error& e) {
            if (e.code() == asio::error::eof) {
                std::cout << "Connection closed by peer\n";
            } else if (e.code() == asio::error::connection_reset) {
                std::cout << "Connection reset by peer\n";
            } else if (e.code() == asio::error::operation_aborted) {
                std::cout << "Operation aborted (likely shutdown)\n";
            } else {
                std::cerr << "System error: " << e.what() << "\n";
            }
            stop();
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            stop();
        }
    }

    awaitable<void> writer() {
        try {
            for (;;) {
                auto out = co_await channel_.async_receive(use_awaitable);
                co_await asio::async_write(
                    socket_, asio::buffer(out.msg->data(), out.msg->length()), use_awaitable);
                if (out.close_after_send) {
                    stop();
                    co_return;
                }
            }
        } catch (std::exception&) {
            stop();
        }
    }

    void stop() {
        login_deadline_.cancel();  // 兜底:任何路径触发 stop,都顺手取消掉登录超时的挂起等待
        if (auto server = server_.lock()) {
            server->leave(shared_from_this());
        }
        socket_.close();
    }

    tcp::socket socket_;
    std::weak_ptr<ChatServer> server_;
    asio::experimental::concurrent_channel<void(boost::system::error_code, ChannelMessage)>
        channel_;
    int64_t uid_;
    bool is_authenticated_ = false;
    asio::steady_timer login_deadline_;
};

#endif