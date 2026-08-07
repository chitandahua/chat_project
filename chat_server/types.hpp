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
    std::string name;
};
BOOST_DESCRIBE_STRUCT(FriendApplyRow, (), (status, name))

struct FriendRow {
    std::string back;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(FriendRow, (), (back, name))

class UserDetail {
public:
    std::string name;

    UserDetail() = default;
    UserDetail(const std::string& username) : name(username) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserDetail, name)
};

class FriendApplyInfo {
public:
    int status = 0;
    UserDetail user_detail;

    explicit FriendApplyInfo(const FriendApplyRow& row)
        : status(row.status), user_detail(row.name) {}

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

    explicit FriendInfo(const FriendRow& row) : back(row.back), user_detail(row.name) {}

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