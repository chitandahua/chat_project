#ifndef _USER_REPO_HPP_
#define _USER_REPO_HPP_

#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/row.hpp>
#include <boost/mysql/static_results.hpp>
#include <boost/mysql/string_view.hpp>
#include <boost/mysql/with_params.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/optional/optional.hpp>

#include <boost/describe/class.hpp>

#include <cstdint>
#include <memory>

#include <map>
#include <mutex>
#include <optional>
#include <tl/expected.hpp>

#include "nlohmann/json.hpp"

namespace mysql = boost::mysql;

struct UserInfo {
    int64_t id;
    std::string name;
    std::string email;
    std::string pwd;
};
BOOST_DESCRIBE_STRUCT(UserInfo, (), (id, name, email, pwd))

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

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserLoginResponse, uid, token, name)
};

class UserRepo {
public:
    UserRepo(std::shared_ptr<mysql::connection_pool>& pool)
        : pool_(pool) {}  // user_infos_(std::make_shared<std::map<int64_t, UserInfo>>())

    auto get_user_info_memory(int64_t uid) const -> std::optional<UserInfo> {
        std::lock_guard lock(mutex_);
        auto it = user_infos_.find(uid);
        if (it == user_infos_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    auto set_user_info_memory(int64_t uid, const UserInfo& info) -> bool {
        std::lock_guard lock(mutex_);
        auto it = user_infos_.find(uid);
        if (it == user_infos_.end()) {
            user_infos_.insert({uid, info});
            return true;
        }
        // TODO 在查数据库时 可能有其他的插入/修改了？
        // it->second = info;
        return false;
    }

    auto get_user_info(int64_t uid) const -> boost::asio::awaitable<tl::expected<UserInfo, int>> {
        auto conn = co_await pool_->async_get_connection();
        mysql::static_results<UserInfo> result;
        co_await conn->async_execute(mysql::with_params("SELECT * FROM user WHERE id = {0}", uid),
                                     result);

        conn.return_without_reset();
        auto user_info = result.rows<0>();
        if (user_info.empty()) {
            co_return tl::make_unexpected(-1);
        }
        co_return user_info[0];
    }

private:
    std::shared_ptr<mysql::connection_pool> pool_;
    mutable std::mutex mutex_;
    // TODO 清除
    // std::shared_ptr<std::map<int64_t, UserInfo>> user_infos_;
    std::map<int64_t, UserInfo> user_infos_;
};
#endif