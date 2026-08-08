#include <boost/redis/request.hpp>
#include <boost/url.hpp>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

#include <boost/asio/detached.hpp>

#include <boost/mysql/any_address.hpp>
#include <boost/mysql/pool_params.hpp>

#include <iostream>
#include <optional>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>

#include <boost/describe/class.hpp>

#include <magic_enum/magic_enum.hpp>

#include "chat_server.hpp"
#include "config.hpp"
#include "friend_repo.hpp"
#include "grpc_service.hpp"
#include "handle_message.hpp"
#include "message_common.hpp"
#include "user_repo.hpp"

namespace asio = boost::asio;
namespace mysql = boost::mysql;
namespace redis = boost::redis;
namespace http = boost::beast::http;
using namespace std::chrono_literals;

class MessageData {
public:
    const MsgNode& msg;

    std::shared_ptr<ChatSession>& session;
    std::shared_ptr<RedisClient>& redis_client;
    mysql::connection_pool& mysql_pool;
    std::shared_ptr<grpc::Channel>& status_grpc_channel;
    std::shared_ptr<grpc::Channel>& peer_grpc_channel;

    UserRepo& user_repo;
};

MessageHandler::MessageHandler(std::shared_ptr<RedisClient>& redis_client,
                               std::shared_ptr<mysql::connection_pool>& pool)
    : redis_client_(redis_client), mysql_pool_(pool), user_repositoty_(pool) {}

ChannelMessage empty_response(const MsgNode& msg) {
    return ChannelMessage(MsgNode(msg.id(), nullptr));
}

ChannelMessage empty_response(int response_id) {
    return ChannelMessage(MsgNode(response_id, nullptr));
}

ChannelMessage message_response(int response_id, nlohmann::json&& response = nlohmann::json()) {
    response["error"] = magic_enum::enum_integer(ServerError::Success);
    response["message"] = magic_enum::enum_name(ServerError::Success);
    return ChannelMessage(MsgNode(response_id, response.dump()));
}

ChannelMessage error_response(int response_id, ServerError error) {
    nlohmann::json response;
    response["error"] = magic_enum::enum_integer(error);
    response["message"] = magic_enum::enum_name(error);
    return ChannelMessage(MsgNode(response_id, response.dump()));
}

ChannelMessage error_response_and_close(int response_id, ServerError error) {
    auto msg = error_response(response_id, error);
    msg.close_after_send = true;
    return msg;
}

// TODO 放到公共库
template <typename T>
concept JsonSerializable = requires(const T& t, nlohmann::json& j) { to_json(j, t); };

template <typename T>
concept JsonDeserializable = requires(const nlohmann::json& j, T& t) { from_json(j, t); };

template <JsonSerializable T>
http::response<http::string_body> nlohmann_json_response(const T& body) {
    http::response<http::string_body> res;

    res.set("Content-Type", "application/json");
    res.body() = nlohmann::json(body).dump();

    return res;
}

template <JsonDeserializable T>
tl::expected<T, std::string> nlohmann_parse_json(std::string_view json_string) {
    try {
        auto json = nlohmann::json::parse(json_string);
        return json.get<T>();
    } catch (const nlohmann::json::exception& e) {
        return tl::make_unexpected(e.what());
    }
}

nlohmann::json response_payload(std::optional<nlohmann::json> body = std::nullopt, int err = 0,
                                const std::string& msg = "") {
    nlohmann::json j = {};
    j["status"] = static_cast<uint8_t>(err);
    j["message"] = msg;
    if (body.has_value()) {
        j["data"] = std::move(body.value());
    }
    return j;
}

nlohmann::json response_payload_empty(int err = 0, const std::string& msg = "") {
    return response_payload(std::nullopt, err, msg);
}

// boost::json::serialize(boost::json::value_from(body));

// Attempts to parse a string as a JSON into an object of type T.
// T should be a type with Boost.Describe metadata.
template <class T>
boost::system::result<T> parse_json(std::string_view json_string) {
    boost::system::error_code ec;
    auto val = boost::json::parse(json_string, ec);
    if (ec) {
        return ec;
    }

    return boost::json::try_value_to<T>(val);
}

template <typename T>
asio::awaitable<std::optional<UserInfo>> search_user_info(const MessageData& input,
                                                          const std::string& user_info_key,
                                                          const T& key, bool& is_redis_exist) {
    // 先查redis
    std::optional<std::string> opt_value = co_await input.redis_client->get(user_info_key);
    if (opt_value.has_value()) {
        auto user_info = parse_json<UserInfo>(opt_value.value());
        if (user_info) {
            is_redis_exist = true;
            co_return user_info.value();
        }
    }

    std::cout << "redis not exist: " << user_info_key << ", search mysql\n";
    // 没有则查mysql
    auto user_info_mysql = co_await input.user_repo.get_user_info(key);
    if (!user_info_mysql) {
        co_return std::nullopt;
    }
    co_return user_info_mysql.value();
}

asio::awaitable<void> save_user_info(const MessageData& input, const std::string& key,
                                     const std::string& user_info_str) {
    (void)co_await input.redis_client->set(key, user_info_str);
    co_return;
}

template <typename T>
awaitable<std::optional<UserInfo>> search_user_by_type(const MessageData& input, const T& key) {
    std::string user_info_key;
    if constexpr (std::is_same_v<T, int64_t>) {
        user_info_key = std::string(UserUidInfoPrefix) + std::to_string(key);
    } else if constexpr (std::is_same_v<T, std::string>) {
        user_info_key = std::string(UserNameInfoPrefix) + key;
    } else {
        static_assert(sizeof(T) == 0, "Unsupported type for search_user_by_type");
    }

    bool is_redis_exist = false;
    auto result = co_await search_user_info<T>(input, user_info_key, key, is_redis_exist);
    if (!result) {
        co_return std::nullopt;
    }
    std::cout << "search user info: " << user_info_key << " " << result.value().name << "\n";
    auto user_info_str = boost::json::serialize(boost::json::value_from(result.value()));
    if (!is_redis_exist) {
        // 保存到redis
        co_await save_user_info(input, user_info_key, user_info_str);
    }
    co_return result;
}

// redis记录 用户登录某个chat_server
asio::awaitable<bool> set_user_login_server(const std::shared_ptr<RedisClient>& redis_client,
                                            int64_t uid, const std::string& server_name) {
    auto key = std::string(UserLoginServerPrefix) + std::to_string(uid);
    co_return co_await redis_client->set(key, server_name);
}

asio::awaitable<std::optional<std::string>> get_user_login_server(
    std::shared_ptr<RedisClient>& redis_client, int64_t uid) {
    auto key = std::string(UserLoginServerPrefix) + std::to_string(uid);
    co_return co_await redis_client->get(key);
}

asio::awaitable<bool> update_login_count(std::shared_ptr<RedisClient>& redis_client,
                                         ChatSession& session) {
    auto result = co_await redis_client->hset<std::string, int64_t>(
        LoginCountKey, std::map<std::string, int64_t>{
                           {session.server()->name(), session.server()->participant_count()}});
    if (result < 0) {
        co_return false;
    }

    std::cout << "update server login count: " << session.server()->name()
              << " count: " << session.server()->participant_count() << "\n";
    co_return true;
}

class UserLoginRequest {
public:
    int64_t uid;
    std::string token;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserLoginRequest, uid, token)
};

class UserLoginResponse {
public:
    int64_t uid;
    std::string token;
    std::string name;
    std::vector<FriendApplyInfo> apply_list;
    std::vector<FriendInfo> friend_list;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserLoginResponse, uid, token, name, apply_list, friend_list)
};

asio::awaitable<ChannelMessage> login(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::LoginResponse);
    auto request = nlohmann_parse_json<UserLoginRequest>(input.msg.body());
    if (!request) {
        co_return error_response_and_close(input.msg.id(), ServerError::InvalidJson);
    }

    auto uid = request.value().uid;
    // 从redis获取token
    auto key = std::string(UserTokenPrefix) + std::to_string(uid);
    auto token = co_await input.redis_client->get(key);
    if (!token || token.value() != request.value().token) {
        co_return error_response_and_close(response_id, ServerError::TokenInvalid);
    }

    std::cout << "login response: " << token.value() << "\n";
    // 从redis/数据库中获取UserInfo
    std::optional<UserInfo> user_info;
    user_info = co_await search_user_by_type<int64_t>(input, uid);
    if (!user_info) {
        co_return error_response_and_close(response_id, ServerError::InternalError);
    }

    std::cout << "user info: " << user_info.value().name << "\n";
    // 登录数 更新到redis
    bool ok = co_await update_login_count(input.redis_client, *input.session);
    if (!ok) {
        std::cout << "warning: update login count failed\n";
    }

    // session更新uid
    std::cout << "update session uid: " << uid << "\n";
    input.session->set_uid(uid);
    input.session->mark_authenticated();
    // 为用户设置登录ip server的名字 redis SET uid映射server name
    (void)co_await set_user_login_server(input.redis_client, uid, input.session->server()->name());

    // TODO 两次查询合一？
    auto apply_list = co_await FriendApplyRepo(input.mysql_pool).get_friend_applies(uid);
    auto friend_list = co_await FriendRepo(input.mysql_pool).get_friends(uid);
    UserLoginResponse login_response{uid, token.value(), user_info.value().name, apply_list,
                                     friend_list};

    auto json_response = response_payload(nlohmann::json(login_response));
    std::cout << "login response: " << json_response.dump() << "\n";
    co_return MsgNode(response_id, json_response.dump());
}

awaitable<ChannelMessage> search_user(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::SearchUserResponse);
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(input.msg.body());
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "parse json error: " << e.what() << "\n";
        co_return error_response(response_id, ServerError::InvalidJson);
    }

    std::optional<UserInfo> response;
    if (request.contains("uid") && request["uid"].is_number()) {
        response = co_await search_user_by_type<int64_t>(input, request["uid"].get<int64_t>());
    } else if (request.contains("name") && request["name"].is_string()) {
        response =
            co_await search_user_by_type<std::string>(input, request["name"].get<std::string>());
    }

    std::optional<ChannelMessage> response_msg;
    if (response) {
        response_msg = ChannelMessage(MsgNode(
            response_id, boost::json::serialize(boost::json::value_from(response.value()))));
    }
    co_return response_msg.value_or(error_response(response_id, ServerError::UserNotFound));
}

class AddFriendRequest {
public:
    int64_t uid = 0;
    int64_t touid = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddFriendRequest, uid, touid)
};

asio::awaitable<ChannelMessage> add_friend(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::AddFriendResponse);
    auto request = nlohmann_parse_json<AddFriendRequest>(input.msg.body());
    if (!request) {
        co_return error_response(response_id, ServerError::InvalidJson);
    } else if (request.value().uid != input.session->uid() ||
               request.value().uid == request.value().touid) {
        // uid必须为当前用户且目标uid不能为自己
        co_return error_response(response_id, ServerError::UserUidInvalid);
    }
    std::cout << "user " << request.value().uid << " add friend " << request.value().touid << "\n";
    // 更新数据库 记录添加好友请求(TODO 同意/用户上线时判断是否已经添加了 则去除？)
    (void)co_await FriendApplyRepo(input.mysql_pool)
        .add_friend_apply(FriendApply{request.value().uid, request.value().touid});

    // redis查询friend是否登录某个chat_server
    auto target_login_server =
        co_await get_user_login_server(input.redis_client, request.value().touid);
    if (!target_login_server) {  // 未登录直接返回
        co_return error_response(response_id, ServerError::Success);
    }

    // 获取当前用户信息
    auto user_info = co_await input.user_repo.get_user_info(request.value().uid);
    if (!user_info) {
        co_return error_response(response_id, ServerError::InternalError);
    }

    // 若登录的是当前chat_server 则直接发送notify消息
    if (target_login_server.value() == input.session->server()->name()) {
        std::cout << "send notify to " << target_login_server.value() << "\n";
        std::shared_ptr<ChatSession> target_session = std::dynamic_pointer_cast<ChatSession>(
            input.session->server()->get_participant(request.value().touid));
        if (!target_session) {  // 可能断连接了？
            std::cout << "session " << request.value().touid << " not found\n";
            co_return error_response(response_id, ServerError::Success);
        }
        // 发送notify消息
        auto notify_msg = NotifyAddFriendMsg(request.value().uid, user_info.value().name);
        target_session->deliver(MsgNode(magic_enum::enum_integer(MessageId::NotifyAddFriend),
                                        nlohmann::json(notify_msg).dump()));
    } else {
        // 否则通过grpc将当前用户信息+好友请求信息 发送给该chat_server
        std::cout << "send grpc notify to " << target_login_server.value() << "\n";
        auto response = ChatServiceClient::notify_add_friend(
            input.peer_grpc_channel,
            GrpcAddFriendRequest{request.value().uid, user_info.value().name,
                                 request.value().touid});
        if (!response || response.value().error != 0) {
            co_return error_response(input.msg.id(), ServerError::RPCFailed);
        }
    }
    co_return message_response(response_id);
}

class AuthFriendRequest {
public:
    int64_t fromuid;  // 认证方uid
    int64_t touid;    // 申请方uid

    explicit operator NotifyAuthFriendMsg() const {
        NotifyAuthFriendMsg notify_msg;
        notify_msg.fromuid = fromuid;
        notify_msg.touid = touid;
        return notify_msg;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AuthFriendRequest, fromuid, touid)
};

asio::awaitable<ChannelMessage> auth_friend(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::AuthFriendResponse);

    auto request = nlohmann_parse_json<AuthFriendRequest>(input.msg.body());
    if (!request) {
        co_return error_response(response_id, ServerError::InvalidJson);
    } else if (request.value().fromuid != input.session->uid() ||
               request.value().fromuid == request.value().touid) {
        co_return error_response(response_id, ServerError::UserUidInvalid);
    }

    std::cout << "user " << request.value().fromuid << " auth friend " << request.value().touid
              << "\n";

    // 更新数据库 记录好友申请已通过以及记录friend信息
    (void)co_await FriendApplyRepo(input.mysql_pool)
        .auth_friend_apply(FriendApply{request.value().fromuid, request.value().touid});

    // 告知to_uid from_uid已通过to_uid的好友申请
    // 若to_uid本机连接 则直接notify 否则通过grpc发送notify

    // redis查询to_uid是否登录某个chat_server
    auto target_login_server =
        co_await get_user_login_server(input.redis_client, request.value().touid);
    if (!target_login_server) {  // 未登录直接返回
        co_return error_response(response_id, ServerError::Success);
    }

    // 若to_uid登录的是当前chat_server 则直接发送notify消息
    if (target_login_server.value() == input.session->server()->name()) {
        std::cout << "send notify to " << target_login_server.value() << "\n";
        std::shared_ptr<ChatSession> target_session = std::dynamic_pointer_cast<ChatSession>(
            input.session->server()->get_participant(request.value().touid));
        if (!target_session) {
            std::cout << "session " << request.value().touid << " not found\n";
            co_return error_response(response_id, ServerError::Success);
        }
        // 获取当前用户from_uid信息
        // auto cur_user_info = co_await input.user_repo.get_user_info(request.value().from_uid);
        // if (!cur_user_info) {
        //    co_return error_response(response_id, ServerError::InternalError);
        //}

        // 发送notify消息
        auto notify_msg = static_cast<NotifyAuthFriendMsg>(request.value());
        target_session->deliver(MsgNode(magic_enum::enum_integer(MessageId::NotifyAuthFriend),
                                        nlohmann::json(notify_msg).dump()));
    } else {
        // 否则通过grpc将当前用户信息+好友请求信息 发送给该chat_server
        std::cout << "send grpc notify to " << target_login_server.value() << "\n";
        auto response = ChatServiceClient::notify_auth_friend(
            input.peer_grpc_channel,
            GrpcAuthFriendRequest{request.value().fromuid, request.value().touid});
        if (!response || response.value().error != 0) {
            co_return error_response(input.msg.id(), ServerError::RPCFailed);
        }
    }
    // TODO 获取原好友请求用户(to_uid)信息 携带返回
    // auto user_info = co_await input.user_repo.get_user_info(request.value().to_uid);
    // if (!user_info) {
    //    co_return error_response(response_id, ServerError::InternalError);
    //}
    co_return message_response(response_id);
}

class TextChatMsgRequest {
public:
    int64_t fromuid = 0;
    int64_t touid = 0;
    std::vector<TextChatData> text_array;

    explicit operator NotifyTextChatMessage() {
        NotifyTextChatMessage notify_msg;
        notify_msg.fromuid = fromuid;
        notify_msg.touid = touid;
        notify_msg.text_array = std::move(text_array);
        return notify_msg;
    }

    explicit operator GrpcTextChatMsgRequest() {
        GrpcTextChatMsgRequest request;
        request.from_uid = fromuid;
        request.to_uid = touid;
        request.text_array = std::move(text_array);
        return request;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TextChatMsgRequest, fromuid, touid, text_array)
};

asio::awaitable<ChannelMessage> text_chat_msg(const MessageData& input) {
    auto response_id = magic_enum::enum_integer(MessageId::TextChatMsgRsp);

    auto request = nlohmann_parse_json<TextChatMsgRequest>(input.msg.body());
    if (!request) {
        co_return error_response(response_id, ServerError::InvalidJson);
    } else if (request.value().fromuid != input.session->uid() ||
               request.value().fromuid == request.value().touid) {
        co_return error_response(response_id, ServerError::UserUidInvalid);
    }

    std::cout << "user " << request.value().fromuid << " send text chat msg to "
              << request.value().touid << "\n";

    // redis查询to_uid是否登录某个chat_server
    auto target_login_server =
        co_await get_user_login_server(input.redis_client, request.value().touid);
    if (!target_login_server) {  // 未登录直接返回
        co_return error_response(response_id, ServerError::Success);
    }

    // 若to_uid登录的是当前chat_server 则直接发送notify消息
    if (target_login_server.value() == input.session->server()->name()) {
        std::cout << "send notify to " << target_login_server.value() << "\n";
        std::shared_ptr<ChatSession> target_session = std::dynamic_pointer_cast<ChatSession>(
            input.session->server()->get_participant(request.value().touid));
        if (!target_session) {
            std::cout << "session " << request.value().touid << " not found\n";
            co_return error_response(response_id, ServerError::Success);
        }

        // 发送notify消息
        auto notify_msg = static_cast<NotifyTextChatMessage>(request.value());
        target_session->deliver(MsgNode(magic_enum::enum_integer(MessageId::NotifyTextChatMsg),
                                        nlohmann::json(notify_msg).dump()));
    } else {
        // 否则通过grpc将信息 发送给该chat_server
        std::cout << "send grpc notify to " << target_login_server.value() << "\n";
        auto response = ChatServiceClient::notify_text_chat_msg(
            input.peer_grpc_channel, static_cast<GrpcTextChatMsgRequest>(request.value()));
        if (!response || response.value().error != 0) {
            co_return error_response(input.msg.id(), ServerError::RPCFailed);
        }
    }

    co_return message_response(response_id);
}

void log_mysql_error(boost::system::error_code ec, const mysql::diagnostics& diag) {
    std::cerr << "MySQL error: " << ec << " " << ec.message();

    if (!diag.client_message().empty()) {
        std::cerr << ": " << diag.client_message();
    }

    if (!diag.server_message().empty()) {
        std::cerr << ": " << diag.server_message();
    }

    std::cerr << std::endl;
}

asio::awaitable<ChannelMessage> MessageHandler::handle_message(std::shared_ptr<ChatSession> session,
                                                               const MsgNode& msg) {
    // 除了登录消息本身,其他消息一律要求先登录过
    if (!session->is_authenticated() &&
        msg.id() != magic_enum::enum_integer(MessageId::LoginRequest)) {
        co_return error_response_and_close(msg.id(), ServerError::NotAuthenticated);
    }

    auto message_id = magic_enum::enum_cast<MessageId>(msg.id());
    if (!message_id) {
        co_return empty_response(msg);
    }

    auto it = handlers_.find(message_id.value());
    if (it == handlers_.end()) {
        co_return empty_response(msg);
    }

    auto response_id = magic_enum::enum_integer(get_response_id(message_id.value()));
    try {
        co_return co_await it->second(MessageData{msg, session, redis_client_, *mysql_pool_,
                                                  status_grpc_channel_, peer_grpc_channel_,
                                                  user_repositoty_});
    } catch (const mysql::error_with_diagnostics& err) {
        log_mysql_error(err.code(), err.get_diagnostics());

        co_return error_response(response_id, ServerError::InternalError);
    } catch (const std::exception& err) {
        std::cerr << "Uncaught exception: " << err.what() << std::endl;
        co_return error_response(response_id, ServerError::InternalError);
    }
}

int MessageHandler::init(const ChatConfig& config) {
    // status grpc
    auto status_target =
        config.status_server.host + ":" + std::to_string(config.status_server.port);
    status_grpc_channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(status_target, grpc::InsecureChannelCredentials()));
    if (!status_grpc_channel_) {
        std::cerr << "Failed to create status gRPC channel. Invalid address or port." << "\n";
        return -1;
    }
    // chat grpc
    auto peer_target =
        config.peer_grpc_server.host + ":" + std::to_string(config.peer_grpc_server.port);
    peer_grpc_channel_ = std::shared_ptr<grpc::Channel>(
        grpc::CreateChannel(peer_target, grpc::InsecureChannelCredentials()));
    if (!peer_grpc_channel_) {
        std::cerr << "Failed to create chat gRPC channel. Invalid address or port." << "\n";
        return -1;
    }

    // mysql
    // mysql_conn_->init(config.mysql_config);
    // mysql_pool_->pool()->async_run(asio::detached);

    handlers_init();
    return 0;
}

void MessageHandler::handlers_init() {
    handlers_.insert({MessageId::LoginRequest, login});
    handlers_.insert({MessageId::SearchUserRequest, search_user});
    handlers_.insert({MessageId::AddFriendRequest, add_friend});
    handlers_.insert({MessageId::AuthFriendRequest, auth_friend});
    handlers_.insert({MessageId::TextChatMsgReq, text_chat_msg});
}
