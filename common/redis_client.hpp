#ifndef _REDIS_CLIENT_HPP_
#define _REDIS_CLIENT_HPP_

#include <memory>

#include <boost/redis/connection.hpp>

#include "nlohmann/json.hpp"

class RedisConfig {
public:
    std::string host;
    int port;
    std::string user;
    std::string pass;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RedisConfig, host, port, user, pass)
};

class RedisClient : std::enable_shared_from_this<RedisClient> {
private:
    int get_config(const RedisConfig& redis_config, boost::redis::config& cfg) {
        if (!redis_config.user.empty()) {
            cfg.use_setup = true;
            cfg.setup.clear();  // Remove the default HELLO 3
            cfg.setup.hello(redis_config.user, redis_config.pass);
        }

        cfg.addr.host = redis_config.host;
        cfg.addr.port = std::to_string(redis_config.port);

        return 0;
    }

public:
    auto run(const RedisConfig& config) -> boost::asio::awaitable<void> {
        boost::redis::config cfg;
        get_config(config, cfg);

        conn_ =
            std::make_shared<boost::redis::connection>(co_await boost::asio::this_coro::executor);
        conn_->async_run(cfg, boost::asio::consign(boost::asio::detached, conn_));
    }

    std::shared_ptr<boost::redis::connection> conn_;
};

#endif