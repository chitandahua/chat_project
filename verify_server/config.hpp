#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#include <fstream>
#include <iostream>
#include <string>

#include "nlohmann/json.hpp"

class EmailConfig {
public:
    std::string user;
    std::string pass;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(EmailConfig, user, pass)
};

class RedisConfig {
public:
    std::string host;
    int port;
    std::string user;
    std::string pass;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RedisConfig, host, port, user, pass)
};

class VerifyConfig {
public:
    int init(const std::string& config_file) {
        std::ifstream config_stream(config_file);
        if (!config_stream.is_open()) {
            std::cout << "config file not found" << "\n";
            return -1;
        }

        try {
            *this = nlohmann::json::parse(config_stream).get<VerifyConfig>();
            // TODO schema validation
        } catch (const std::exception& e) {
            std::cout << "config file" << config_file << " parse failed: " << e.what() << "\n";
            return -1;
        }

        return 0;
    }

    EmailConfig email;
    RedisConfig redis;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(VerifyConfig, email, redis)
};

#endif