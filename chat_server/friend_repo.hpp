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

#include "types.hpp"

namespace mysql = boost::mysql;

class FriendApplyRepo {
public:
    explicit FriendApplyRepo(mysql::connection_pool& pool) : pool_(pool) {}

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

    awaitable<std::vector<FriendApplyInfo>> get_friend_applies(int64_t to_uid) {
        auto conn = co_await pool_.async_get_connection();

        boost::mysql::static_results<FriendApplyRow> result;
        co_await conn->async_execute(
            boost::mysql::with_params("SELECT fa.status AS status, fa.from_uid AS from_uid, "
                                      "u.name AS name "
                                      "FROM friend_apply fa JOIN user u ON u.id = fa.from_uid "
                                      "WHERE fa.to_uid = {}",
                                      to_uid),
            result);

        std::vector<FriendApplyInfo> list;
        list.reserve(result.rows().size());
        for (auto& row : result.rows()) {
            list.push_back(FriendApplyInfo(row));
        }
        co_return list;
    }

    awaitable<std::vector<FriendInfo>> get_friends(int64_t self_id) {
        auto conn = co_await pool_.async_get_connection();

        boost::mysql::static_results<FriendRow> result;
        co_await conn->async_execute(
            boost::mysql::with_params("SELECT f.friend_id AS friend_id, f.back AS back, "
                                      "u.name AS name "
                                      "FROM friend f JOIN user u ON u.id = f.friend_id "
                                      "WHERE f.self_id = {}",
                                      self_id),
            result);

        std::vector<FriendInfo> list;
        list.reserve(result.rows().size());
        for (auto& row : result.rows()) {
            list.push_back(FriendInfo(row));
        }
        co_return list;
    }

private:
    mysql::connection_pool& pool_;
};

class FriendRepo {
public:
    explicit FriendRepo(mysql::connection_pool& pool) : pool_(pool) {}

    awaitable<std::vector<FriendInfo>> get_friends(int64_t self_id) {
        auto conn = co_await pool_.async_get_connection();

        boost::mysql::static_results<FriendRow> result;
        co_await conn->async_execute(
            boost::mysql::with_params("SELECT f.friend_id AS friend_id, f.back AS back, "
                                      "u.name AS name "
                                      "FROM friend f JOIN user u ON u.id = f.friend_id "
                                      "WHERE f.self_id = {}",
                                      self_id),
            result);

        std::vector<FriendInfo> list;
        list.reserve(result.rows().size());
        for (auto& row : result.rows()) {
            list.push_back(FriendInfo(row));
        }
        co_return list;
    }

private:
    mysql::connection_pool& pool_;
};

#endif