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
};

namespace magic_enum::customize {
template <>
struct enum_range<MessageId> {
    static constexpr int min = 1005;
    static constexpr int max = 2000;
};
}  // namespace magic_enum::customize

// 保存redis的key前缀
constexpr std::string_view UserUidInfoPrefix = "user_uid_info_";
constexpr std::string_view UserNameInfoPrefix = "user_name_info_";

#endif