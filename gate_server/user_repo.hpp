#ifndef _USER_REPO_HPP_
#define _USER_REPO_HPP_

#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/static_results.hpp>
#include <boost/mysql/string_view.hpp>

#include <boost/asio/spawn.hpp>
#include <boost/optional/optional.hpp>

#include <boost/describe/class.hpp>

#include <cstdint>
#include <memory>

#include <tl/expected.hpp>
#include "nlohmann/json.hpp"

struct UserInfo {
    int64_t id;
    std::string user;
    std::string email;
    std::string passwd;
};
BOOST_DESCRIBE_STRUCT(UserInfo, (), (id, user, email, passwd))

class UserRegisterRequest {
public:
    std::string user;
    std::string email;
    std::string passwd;
    std::string verify_code;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserRegisterRequest, user, email, passwd, verify_code)
};

class UserRepo {
public:
    UserRepo(std::shared_ptr<boost::mysql::connection_pool>& pool) noexcept : pool_(pool) {}

    auto create_user(const UserRegisterRequest& req) -> tl::expected<UserInfo, std::string> {
        try {
            boost::mysql::pooled_connection conn =
                pool_->async_get_connection(boost::asio::use_future).get();

            boost::mysql::statement stmt =
                conn->async_prepare_statement(
                        "INSERT INTO user (user, email, passwd) VALUES (?, ?, ?)",
                        boost::asio::use_future)
                    .get();

            boost::mysql::static_results<std::tuple<>> result;
            conn->async_execute(stmt.bind(req.user, req.email, req.passwd), result,
                                boost::asio::use_future)
                .get();

            auto new_id = static_cast<std::int64_t>(result.last_insert_id());
            if (new_id == 0) {
                return tl::make_unexpected("insert failed: no id generated");
            }

            return UserInfo{new_id, req.user, req.email, req.passwd};
        } catch (const boost::mysql::error_with_diagnostics& e) {
            if (e.code().value() == 1062) {
                return tl::make_unexpected(std::string("user or email exist"));
            }
            return tl::make_unexpected(
                std::string(e.what()));  // + " | " +e.get_diagnostics().server_message()
        } catch (const std::exception& e) {
            return tl::make_unexpected(e.what());
        }
    }

private:
    std::shared_ptr<boost::mysql::connection_pool> pool_;
};
#endif