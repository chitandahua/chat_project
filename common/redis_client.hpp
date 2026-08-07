#ifndef _REDIS_CLIENT_HPP_
#define _REDIS_CLIENT_HPP_

#include <map>
#include <memory>
#include <optional>

#include <boost/asio.hpp>
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
    boost::asio::awaitable<std::optional<std::string>> get(const std::string& key);
    boost::asio::awaitable<bool> set(const std::string& key, const std::string& value);
    boost::asio::awaitable<bool> set_expired(const std::string& key, const std::string& value,
                                             int ttl);
    template <typename K, typename V>
    boost::asio::awaitable<int> hset(const std::string& key, std::map<K, V>&& map);
    template <typename K, typename V>
    boost::asio::awaitable<std::map<K, V>> hgetall(const std::string& key);

    auto run(const RedisConfig& config) -> boost::asio::awaitable<void> {
        boost::redis::config cfg;
        get_config(config, cfg);

        conn_ =
            std::make_shared<boost::redis::connection>(co_await boost::asio::this_coro::executor);
        conn_->async_run(cfg, boost::asio::consign(boost::asio::detached, conn_));
    }

    std::shared_ptr<boost::redis::connection> conn_;
};

extern template boost::asio::awaitable<int> RedisClient::hset<std::string, int64_t>(
    const std::string& key, std::map<std::string, int64_t>&& map);
extern template boost::asio::awaitable<std::map<std::string, int64_t>>
RedisClient::hgetall<std::string, int64_t>(const std::string& key);

#endif