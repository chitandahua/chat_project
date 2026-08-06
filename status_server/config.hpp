#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#include <fstream>
#include <iostream>
#include <string>

#include "nlohmann/json.hpp"
#include "redis_client.hpp"

class ServerConfig {
public:
    std::string name;
    std::string host;
    int port;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServerConfig, name, host, port)
};

class EmailConfig {
public:
    std::string user;
    std::string pass;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(EmailConfig, user, pass)
};

class StatusConfig {
public:
    int init(const std::string& config_file) {
        std::ifstream config_stream(config_file);
        if (!config_stream.is_open()) {
            std::cout << "config file not found" << "\n";
            return -1;
        }

        try {
            *this = nlohmann::json::parse(config_stream).get<StatusConfig>();
            // TODO schema validation
        } catch (const std::exception& e) {
            std::cout << "config file" << config_file << " parse failed: " << e.what() << "\n";
            return -1;
        }

        return 0;
    }

    ServerConfig server;
    RedisConfig redis;
    std::vector<ServerConfig> chat_servers;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(StatusConfig, server, redis, chat_servers)
};

#endif