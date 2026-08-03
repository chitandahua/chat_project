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

class Config {
    bool read_config() {
        std::string path = "../config/gate_server.toml";
        auto config = toml::parse_file(path);

        if (!server_config.from_toml(config, "server") || !server_config.valid()) {
            return false;
        }

        if (!rpc_server_config.from_toml(config, "verify") || !rpc_server_config.valid()) {
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
    ServerConfig rpc_server_config;
};

#endif