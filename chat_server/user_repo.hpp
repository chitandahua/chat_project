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

    auto get_user_info(const std::string& name) const
        -> boost::asio::awaitable<tl::expected<UserInfo, int>> {
        auto conn = co_await pool_->async_get_connection();
        mysql::static_results<UserInfo> result;
        co_await conn->async_execute(
            mysql::with_params("SELECT * FROM user WHERE name = {0}", name), result);

        conn.return_without_reset();
        auto user_info = result.rows<0>();
        if (user_info.empty()) {
            co_return tl::make_unexpected(-1);
        }
        co_return user_info[0];
    }

private:
    std::shared_ptr<mysql::connection_pool> pool_;
};
#endif