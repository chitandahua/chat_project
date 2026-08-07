#ifndef _HANDLE_MESSAGE_HPP_
#define _HANDLE_MESSAGE_HPP_

#include "msg_node.hpp"
#include "user_repo.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http.hpp>

#include <grpcpp/grpcpp.h>
#include <map>
#include <memory>

#include "message_common.hpp"

using namespace boost::asio::ip;
namespace asio = boost::asio;

class ChatConfig;
class MsgNode;
class MessageData;
class ChatSession;
class RedisClient;

class ChannelMessage {
public:
    ChannelMessage() = default;
    explicit ChannelMessage(const MsgNode& msg_node) : msg(std::make_shared<MsgNode>(msg_node)) {}
    explicit ChannelMessage(MsgNode&& msg_node)
        : msg(std::make_shared<MsgNode>(std::move(msg_node))) {}
    ChannelMessage(MsgNode&& msg_node, bool close)
        : msg(std::make_shared<MsgNode>(std::move(msg_node))), close_after_send(close) {}

    std::shared_ptr<MsgNode> msg;
    bool close_after_send = false;  // 这条消息发完之后,是否要关闭连接
};

using RequestHandler = std::function<asio::awaitable<ChannelMessage>(const MessageData&)>;

class MessageHandler {
public:
    MessageHandler(std::shared_ptr<RedisClient>& redis_client,
                   std::shared_ptr<mysql::connection_pool>& pool);

    int init(const ChatConfig& config);
    asio::awaitable<ChannelMessage> handle_message(std::shared_ptr<ChatSession> session,
                                                   const MsgNode& msg);

private:
    void handlers_init();

    std::shared_ptr<RedisClient> redis_client_;
    std::shared_ptr<mysql::connection_pool> mysql_pool_;
    std::shared_ptr<grpc::Channel> status_grpc_channel_;
    std::shared_ptr<grpc::Channel> peer_grpc_channel_;
    std::map<MessageId, RequestHandler> handlers_;
    UserRepo user_repositoty_;
};

#endif