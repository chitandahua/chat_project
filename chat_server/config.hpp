#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#include <fstream>
#include <iostream>
#include <string>

#include "nlohmann/json.hpp"
#include "redis_client.hpp"

class ServerConfig {
public:
    std::string host;
    int port;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServerConfig, host, port)
};

class ChatServerConfig {
public:
    std::string name;
    std::string host;
    int port;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ChatServerConfig, name, host, port)
};

class MysqlConfig {
public:
    std::string host;
    int port;
    std::string user;
    std::string pass;
    std::string database;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(MysqlConfig, host, port, user, pass, database)
};

class ChatConfig {
public:
    int init(const std::string& config_file) {
        std::ifstream config_stream(config_file);
        if (!config_stream.is_open()) {
            std::cout << "config file not found" << "\n";
            return -1;
        }

        try {
            *this = nlohmann::json::parse(config_stream).get<ChatConfig>();
            // TODO schema validation
        } catch (const std::exception& e) {
            std::cout << "config file" << config_file << " parse failed: " << e.what() << "\n";
            return -1;
        }

        return 0;
    }

    ChatServerConfig server;
    ServerConfig status_server;
    ServerConfig grpc_server;
    ServerConfig peer_grpc_server;
    RedisConfig redis;
    MysqlConfig mysql;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ChatConfig, server, status_server, grpc_server, peer_grpc_server,
                                   redis, mysql)
};

#endif