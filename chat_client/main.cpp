// client.cpp
// 简单的 MsgNode 协议手动测试客户端。
//
// 每行输入格式:  <id>|<json内容>
// 例如:          1|{"uid":1,"token":"abc"}
//
// 用法:
//   ./client --host=127.0.0.1 --port=10086                # 交互模式,逐行输入
//   ./client --host=127.0.0.1 --port=10086 --file=req.txt  # 从文件逐行读,自动退出
//
// 交互模式下输入 exit 退出。

#include <boost/asio.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../chat_server/msg_node.hpp"

using boost::asio::ip::tcp;

struct Config {
    std::string host = "127.0.0.1";
    std::string port = "18080";
    std::string file;  // 空表示从标准输入交互式读取
};

Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        if (arg.rfind("--", 0) != 0 || eq == std::string::npos)
            continue;
        std::string key = arg.substr(2, eq - 2);
        std::string val = arg.substr(eq + 1);

        if (key == "host")
            cfg.host = val;
        else if (key == "port")
            cfg.port = val;
        else if (key == "file")
            cfg.file = val;
    }
    return cfg;
}

// 解析一行 "id|json内容",分隔符只取第一个 '|',后面的内容(哪怕 JSON 里也有 |)整体当 body
// 返回 false 表示格式不对(没有分隔符,或者 id 不是合法数字)
bool parse_line(const std::string& line, uint32_t& id, std::string& body) {
    auto pos = line.find('|');
    if (pos == std::string::npos) {
        std::cerr << "格式错误,应为 <id>|<json内容> ,已跳过: " << line << "\n";
        return false;
    }

    std::string id_str = line.substr(0, pos);
    body = line.substr(pos + 1);

    try {
        size_t consumed = 0;
        unsigned long parsed = std::stoul(id_str, &consumed);
        if (consumed != id_str.size()) {
            std::cerr << "id 不是合法数字,已跳过: " << line << "\n";
            return false;
        }
        id = static_cast<uint32_t>(parsed);
    } catch (const std::exception&) {
        std::cerr << "id 不是合法数字,已跳过: " << line << "\n";
        return false;
    }

    return true;
}

// 发送一条消息(自动编码 id + 长度头)
bool send_message(tcp::socket& socket, uint32_t id, const std::string& body,
                  boost::system::error_code& ec) {
    MsgNode node;
    node.set_id(id);
    if (!node.set_body_length(static_cast<int>(body.size()))) {
        std::cerr << "body too large (max " << MsgNode::MAX_LENGTH << " bytes), skipped\n";
        return false;
    }
    node.encode_header();
    std::memcpy(node.body(), body.data(), body.size());

    boost::asio::write(socket, boost::asio::buffer(node.data(), node.length()), ec);
    return !ec;
}

// 收一条完整消息,打印出来
bool recv_and_print(tcp::socket& socket, boost::system::error_code& ec) {
    MsgNode node;
    boost::asio::read(socket, boost::asio::buffer(node.data(), MsgNode::PREFIX_LEN), ec);
    if (ec)
        return false;

    if (!node.decode_header()) {
        std::cerr << "invalid header received\n";
        return false;
    }

    if (node.body_length() > 0) {
        boost::asio::read(socket, boost::asio::buffer(node.body(), node.body_length()), ec);
        if (ec)
            return false;
    }

    std::cout << "recv  id=" << node.id()
              << "  body=" << std::string(node.body(), node.body_length()) << "\n";
    return true;
}

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);

    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);

        boost::asio::connect(socket, resolver.resolve(cfg.host, cfg.port));
        std::cout << "connected to " << cfg.host << ":" << cfg.port << "\n";

        // 输入来源:文件(批量、自动结束)或标准输入(交互、exit 结束)
        std::ifstream file_in;
        std::istream* input = &std::cin;
        bool interactive = cfg.file.empty();

        if (!interactive) {
            file_in.open(cfg.file);
            if (!file_in) {
                std::cerr << "cannot open file: " << cfg.file << "\n";
                return 1;
            }
            input = &file_in;
        } else {
            std::cout << "每行输入 <id>|<json内容> 发送,输入 exit 退出\n> ";
        }

        std::string line;
        while (std::getline(*input, line)) {
            if (interactive && line == "exit") {
                break;
            }
            // 跳过空行与注释行(以 # 开头),便于在请求文件里写说明
            if (line.empty() || line[0] == '#') {
                if (interactive)
                    std::cout << "> ";
                continue;
            }

            uint32_t id = 0;
            std::string body;
            if (!parse_line(line, id, body)) {
                if (interactive)
                    std::cout << "> ";
                continue;
            }

            boost::system::error_code ec;

            if (!send_message(socket, id, body, ec)) {
                if (ec)
                    std::cerr << "send error: " << ec.message() << "\n";
                break;
            }
            std::cout << "send  id=" << id << "  body=" << body << "\n";

            if (!recv_and_print(socket, ec)) {
                if (ec == boost::asio::error::eof) {
                    std::cout << "server closed connection\n";
                } else if (ec) {
                    std::cerr << "recv error: " << ec.message() << "\n";
                }
                break;
            }

            if (interactive)
                std::cout << "> ";
        }

        socket.close();
    } catch (std::exception& e) {
        std::cerr << "exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}