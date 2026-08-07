#ifndef _MESSAGE_COMMON_HPP_
#define _MESSAGE_COMMON_HPP_

#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <string_view>

enum class MessageId : uint16_t {
    LoginRequest = 1005,
    LoginResponse = 1006,
    SearchUserRequest = 1007,
    SearchUserResponse = 1008,
    AddFriendRequest = 1009,
    AddFriendResponse = 1010,
    NotifyAddFriend = 1011,
    AuthFriendRequest = 1013,
    AuthFriendResponse = 1014,
    NotifyAuthFriend = 1015,
    TextChatMsgReq = 1017,
    TextChatMsgRsp = 1018,
    NotifyTextChatMsg = 1019,
    InvalidRequest = 1500,
};

inline MessageId get_response_id(MessageId id) {
    switch (id) {
        case MessageId::LoginRequest:
            return MessageId::LoginResponse;
        case MessageId::SearchUserRequest:
            return MessageId::SearchUserResponse;
        case MessageId::AddFriendRequest:
            return MessageId::AddFriendResponse;
        default:
            return MessageId::InvalidRequest;
    }
}

namespace magic_enum::customize {
template <>
struct enum_range<MessageId> {
    static constexpr int min = 1005;
    static constexpr int max = 2000;
};
}  // namespace magic_enum::customize

enum class ServerError : uint16_t {
    Success = 0,
    InvalidJson = 1001,
    RPCFailed,
    VerifyCodeExpired,
    VerifyCodeErr,
    UserExist,
    PasswdError,
    EmailNotMatch,
    PasswdUpdateFailed,
    TokenInvalid,
    InternalError,
    UserNotFound,
    UserUidInvalid,
    NotAuthenticated,
};

namespace magic_enum::customize {
template <>
struct enum_range<ServerError> {
    static constexpr int min = 1001;
    static constexpr int max = 1024;
};
}  // namespace magic_enum::customize

// 保存redis的key前缀
static const char* UserLoginServerPrefix = "user_login_server_";
static const char* UserUidInfoPrefix = "user_uid_info_";
static const char* UserNameInfoPrefix = "user_name_info_";
static const char* LoginCountKey = "login_count";

#endif