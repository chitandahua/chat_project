#ifndef _FRIEND_REPO_HPP_
#define _FRIEND_REPO_HPP_
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

struct FriendApply {
    int64_t from_uid;
    int64_t to_uid;
};
BOOST_DESCRIBE_STRUCT(FriendApply, (), (from_uid, to_uid))

class FriendApplyRepo {
public:
    FriendApplyRepo(mysql::connection_pool& pool)
        : pool_(pool) {}  // user_infos_(std::make_shared<std::map<int64_t, UserInfo>>())

    auto add_friend_apply(const FriendApply& req) const -> boost::asio::awaitable<bool> {
        auto conn = co_await pool_.async_get_connection();
        mysql::static_results<std::tuple<>> result;
        co_await conn->async_execute(
            mysql::with_params("INSERT INTO friend_apply (from_uid, to_uid) values ({0},{1}) ON "
                               "DUPLICATE KEY UPDATE "
                               "from_uid = from_uid, to_uid = to_uid",
                               req.from_uid, req.to_uid),
            result);

        conn.return_without_reset();
        co_return true;
    }

    auto auth_friend_apply(const FriendApply& req) const -> boost::asio::awaitable<bool> {
        auto conn = co_await pool_.async_get_connection();
        mysql::static_results<std::tuple<>, std::tuple<>, std::tuple<>, std::tuple<>, std::tuple<>>
            result;
        co_await conn->async_execute(
            mysql::with_params(
                "START TRANSACTION;"
                "UPDATE friend_apply SET status = 1 WHERE from_uid = {0} AND to_uid = {1};"
                "INSERT IGNORE INTO friend (self_id, friend_id) VALUES ({0}, {1});"
                "INSERT IGNORE INTO friend (self_id, friend_id) VALUES ({1}, {0});"
                "COMMIT;",
                req.from_uid, req.to_uid),
            result);

        conn.return_without_reset();
        co_return true;
    }

private:
    mysql::connection_pool& pool_;
};

#endif