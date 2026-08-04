#ifndef _MYSQL_CONN_POOL_HPP_
#define _MYSQL_CONN_POOL_HPP_

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>

#include <boost/mysql/connection_pool.hpp>

#include "config.hpp"

class MysqlConnPool {
public:
    MysqlConnPool(boost::asio::io_context& ioc) : ioc_(ioc) {}

    void init(const MysqlConfig& cfg) {
        boost::mysql::pool_params p;
        p.server_address.emplace_host_and_port(cfg.host, cfg.port);
        p.username = cfg.user;
        p.password = cfg.pass;
        p.database = cfg.database;

        pool_ = std::make_shared<boost::mysql::connection_pool>(ioc_, std::move(p));
    }

    std::shared_ptr<boost::mysql::connection_pool> pool() {
        return pool_;
    }

private:
    boost::asio::io_context& ioc_;
    std::shared_ptr<boost::mysql::connection_pool> pool_;
};

#endif