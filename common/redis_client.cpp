
#include <boost/redis/src.hpp>
#include <iostream>

#include "redis_client.hpp"

namespace redis = boost::redis;

boost::asio::awaitable<std::optional<std::string>> RedisClient::get(const std::string& key) {
    redis::request get_req;
    get_req.push("GET", key);
    redis::response<std::optional<std::string>> get_resp;

    boost::system::error_code ec;
    co_await conn_->async_exec(get_req, get_resp,
                               boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis get error (transport/adapt): " << ec.message() << std::endl;
        co_return std::nullopt;
    }

    auto result = std::get<0>(get_resp);
    if (result.has_error()) {
        std::cerr << "redis get error (server): " << result.error().diagnostic << std::endl;
        co_return std::nullopt;
    }

    co_return result.value();
}

boost::asio::awaitable<bool> RedisClient::set(const std::string& key, const std::string& value) {
    boost::redis::request set_req;
    set_req.push("SET", key, value);
    boost::redis::response<std::string> set_resp;
    boost::system::error_code ec;
    co_await conn_->async_exec(set_req, set_resp,
                               boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis set error (transport/adapt): " << ec.message() << std::endl;
        co_return false;
    }

    auto result = std::get<0>(set_resp);
    if (result.has_error()) {
        std::cerr << "redis set error (server): " << result.error().diagnostic << std::endl;
        co_return false;
    }
    co_return true;
}

boost::asio::awaitable<bool> RedisClient::set_expired(const std::string& key,
                                                      const std::string& value, int ttl) {
    boost::redis::request set_req;
    set_req.push("SETEX", key, std::to_string(ttl), value);
    boost::redis::response<std::string> set_resp;
    boost::system::error_code ec;
    co_await conn_->async_exec(set_req, set_resp,
                               boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis set error (transport/adapt): " << ec.message() << std::endl;
        co_return false;
    }

    auto result = std::get<0>(set_resp);
    if (result.has_error()) {
        std::cerr << "redis set error (server): " << result.error().diagnostic << std::endl;
        co_return false;
    }
    co_return true;
}

template <typename K, typename V>
boost::asio::awaitable<int> RedisClient::hset(const std::string& key, std::map<K, V>&& values) {
    boost::redis::request set_req;
    set_req.push_range("HSET", key, values);
    boost::redis::response<int> set_resp;
    boost::system::error_code ec;
    co_await conn_->async_exec(set_req, set_resp,
                               boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis set error (transport/adapt): " << ec.message() << std::endl;
        co_return -1;
    }
    auto result = std::get<0>(set_resp);
    if (result.has_error()) {
        std::cerr << "redis set error (server): " << result.error().diagnostic << std::endl;
        co_return -1;
    }
    co_return result.value();
}

template <typename K, typename V>
boost::asio::awaitable<std::map<K, V>> RedisClient::hgetall(const std::string& key) {
    boost::redis::request get_req;
    get_req.push("HGETALL", key);
    boost::redis::response<std::map<K, V>> get_resp;
    boost::system::error_code ec;
    co_await conn_->async_exec(get_req, get_resp,
                               boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        std::cerr << "redis get error (transport/adapt): " << ec.message() << std::endl;
        co_return std::map<K, V>();
    }
    auto result = std::get<0>(get_resp);
    if (!result) {
        co_return std::map<K, V>();
    }
    co_return result.value();
}

template boost::asio::awaitable<int> RedisClient::hset<std::string, int64_t>(
    const std::string& key, std::map<std::string, int64_t>&& map);
template boost::asio::awaitable<std::map<std::string, int64_t>>
RedisClient::hgetall<std::string, int64_t>(const std::string& key);