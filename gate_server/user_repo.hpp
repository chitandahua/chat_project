#ifndef _USER_REPO_HPP_
#define _USER_REPO_HPP_

#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/static_results.hpp>
#include <boost/mysql/string_view.hpp>
#include <boost/mysql/with_params.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/optional/optional.hpp>

#include <boost/describe/class.hpp>

#include <cstdint>
#include <memory>

#include <tl/expected.hpp>
#include "error.hpp"
#include "nlohmann/json.hpp"

namespace mysql = boost::mysql;

class UserRegisterRequest {
public:
    std::string user;
    std::string email;
    std::string passwd;
    std::string confirm_passwd;
    std::string verify_code;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserRegisterRequest, user, email, passwd, confirm_passwd,
                                   verify_code)
};

class UserRegisterResponse {
public:
    int64_t id;
    std::string user;
    std::string email;
    std::string passwd;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserRegisterResponse, id, user, email, passwd)
};

class UserResetPasswordRequest {
public:
    std::string user;
    std::string email;
    std::string passwd;
    std::string verify_code;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserResetPasswordRequest, user, email, passwd, verify_code)
};

class UserRepo {
public:
    UserRepo(std::shared_ptr<mysql::connection_pool>& pool) noexcept : pool_(pool) {}

    auto create_user(const UserRegisterRequest& req) const
        -> boost::asio::awaitable<tl::expected<UserRegisterResponse, ServiceError>> {
        try {
            auto conn = co_await pool_->async_get_connection();
            auto stmt = co_await conn->async_prepare_statement(
                "INSERT INTO user (name, email, pwd) VALUES (?, ?, ?)");

            mysql::static_results<std::tuple<>> result;
            co_await conn->async_execute(stmt.bind(req.user, req.email, req.passwd), result);

            auto new_id = static_cast<std::int64_t>(result.last_insert_id());

            co_return UserRegisterResponse{new_id, req.user, req.email, req.passwd};
        } catch (const mysql::error_with_diagnostics& e) {
            if (e.code().value() == 1062) {
                co_return tl::make_unexpected(ServiceError::USER_OR_EMAIL_EXIST);
            }
            throw;
        }
    }

    auto reset_password(const UserResetPasswordRequest& req) const -> boost::asio::awaitable<bool> {
        auto conn = co_await pool_->async_get_connection();
        mysql::static_results<std::tuple<>> result;
        co_await conn->async_execute(
            mysql::with_params("UPDATE user SET pwd = {0} WHERE name = {1} AND email = {2}",
                               req.passwd, req.user, req.email),
            result);

        conn.return_without_reset();
        co_return result.affected_rows() != 0;
    }

private:
    std::shared_ptr<mysql::connection_pool> pool_;
};
#endif