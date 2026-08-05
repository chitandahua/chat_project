#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#include <toml++/toml.h>
#include <iostream>
#include <string>

#define TOML_FIELD(field)                               \
    if (auto v = node[#field].value<decltype(field)>()) \
        field = *v;                                     \
    else                                                \
        return false;

class ServerConfig {
public:
    bool from_toml(const toml::table& tbl, std::string_view prefix) {
        auto node = tbl[prefix];
        TOML_FIELD(host);
        TOML_FIELD(port);
        return true;
    }

    bool valid() const {
        return !host.empty() && port >= 1 && port <= 65535;
    }

    std::string host;
    int port;
};

class RedisConfig {
public:
    std::string host;
    int port;
    std::string user;
    std::string pass;

    bool from_toml(const toml::table& tbl, std::string_view prefix) {
        auto node = tbl[prefix];
        TOML_FIELD(host);
        TOML_FIELD(port);
        TOML_FIELD(user);
        TOML_FIELD(pass);
        return true;
    }

    bool valid() const {
        return !host.empty() && port >= 1 && port <= 65535;
    }
};

class MysqlConfig {
public:
    std::string host;
    int port;
    std::string user;
    std::string pass;
    std::string database;

    bool from_toml(const toml::table& tbl, std::string_view prefix) {
        auto node = tbl[prefix];
        TOML_FIELD(host);
        TOML_FIELD(port);
        TOML_FIELD(user);
        TOML_FIELD(pass);
        TOML_FIELD(database);
        return true;
    }

    bool valid() const {
        return !host.empty() && port >= 1 && port <= 65535;
    }
};

class Config {
    bool read_config() {
        std::string path = "../config/gate_server.toml";
        auto config = toml::parse_file(path);

        if (!server_config.from_toml(config, "server") || !server_config.valid()) {
            return false;
        }

        if (!verify_server_config.from_toml(config, "verify") || !verify_server_config.valid()) {
            return false;
        }

        if (!status_server_config.from_toml(config, "status") || !status_server_config.valid()) {
            return false;
        }

        if (!redis_config.from_toml(config, "redis") || !redis_config.valid()) {
            return false;
        }

        if (!mysql_config.from_toml(config, "mysql") || !mysql_config.valid()) {
            return false;
        }

        return true;
    }

public:
    int init() {
        if (!read_config()) {
            std::cerr << "read config error" << "\n";
            return -1;
        }
        return 0;
    }

    ServerConfig server_config;
    ServerConfig verify_server_config;
    ServerConfig status_server_config;
    RedisConfig redis_config;
    MysqlConfig mysql_config;
};

#endif