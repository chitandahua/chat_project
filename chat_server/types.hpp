#ifndef _TYPES_HPP_
#define _TYPES_HPP_

#include <boost/describe/class.hpp>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

struct FriendApply {
    int64_t from_uid;
    int64_t to_uid;
};
BOOST_DESCRIBE_STRUCT(FriendApply, (), (from_uid, to_uid))

struct UserInfo {
    int64_t id;
    std::string name;
    std::string email;
    std::string pwd;
};
BOOST_DESCRIBE_STRUCT(UserInfo, (), (id, name, email, pwd))

// 跟 SQL 里 SELECT 出来的列一一对应,扁平结构
struct FriendApplyRow {
    int32_t status;
    int64_t from_uid;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(FriendApplyRow, (), (status, from_uid, name))

struct FriendRow {
    int64_t friend_id;
    std::optional<std::string> back;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(FriendRow, (), (friend_id, back, name))

class UserDetail {
public:
    int64_t id = 0;
    std::string name;

    UserDetail() = default;
    UserDetail(int64_t uid, const std::string& username) : id(uid), name(username) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserDetail, id, name)
};

class FriendApplyInfo {
public:
    int status = 0;
    UserDetail user_detail;

    explicit FriendApplyInfo(const FriendApplyRow& row)
        : status(row.status), user_detail(row.from_uid, row.name) {}

    friend void to_json(nlohmann::json& j, const FriendApplyInfo& info) {
        j = info.user_detail;
        j["status"] = info.status;
    }

    friend void from_json(const nlohmann::json& j, FriendApplyInfo& info) {
        info.status = j.at("status").get<int>();
        info.user_detail = j.get<UserDetail>();
    }
};

class FriendInfo {
public:
    std::string back;
    UserDetail user_detail;

    explicit FriendInfo(const FriendRow& row)
        : back(row.back.value_or("")), user_detail(row.friend_id, row.name) {}

    friend void to_json(nlohmann::json& j, const FriendInfo& info) {
        j = info.user_detail;
        j["back"] = info.back;
    }

    friend void from_json(const nlohmann::json& j, FriendInfo& info) {
        info.user_detail = j.get<UserDetail>();
        info.back = j.at("back").get<std::string>();
    }
};

#endif