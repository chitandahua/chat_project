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
    bool from_toml(const toml::table& tbl) {
        auto node = tbl["server"];
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

        if (!server_config.from_toml(config) || !server_config.valid()) {
            std::cout << "host " << server_config.host << " port " << server_config.port
                      << " is invalid" << "\n";
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
};

#endif