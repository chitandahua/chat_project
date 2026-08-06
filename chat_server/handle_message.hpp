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

using namespace boost::asio::ip;
namespace asio = boost::asio;

class ChatConfig;
class MsgNode;
class MessageData;
class ChatSession;
class RedisClient;

enum class MessageId : uint8_t {
    Login = 0,
};

using RequestHandler = std::function<asio::awaitable<MsgNode>(const MessageData&)>;

class MessageHandler {
public:
    MessageHandler(std::shared_ptr<RedisClient>& redis_client,
                   std::shared_ptr<mysql::connection_pool>& pool);

    int init(const ChatConfig& config);
    asio::awaitable<MsgNode> handle_message(std::shared_ptr<ChatSession> session,
                                            const MsgNode& msg);

private:
    void handlers_init();

    std::shared_ptr<RedisClient> redis_client_;
    std::shared_ptr<mysql::connection_pool> mysql_pool_;
    std::shared_ptr<grpc::Channel> status_grpc_channel_;
    std::map<MessageId, RequestHandler> handlers_;
    UserRepo user_repositoty_;
};

#endif