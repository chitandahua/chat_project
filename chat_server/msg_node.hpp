#ifndef _MSG_NODE_HPP_
#define _MSG_NODE_HPP_

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>

// 线上格式(依次紧挨着,不含任何 padding):
//   [id: ID_LEN 字节][body_length: HEAD_LEN 字节][body: 变长,最长 MAX_LENGTH]
class MsgNode {
public:
    static constexpr size_t MAX_LENGTH = 1024;
    static constexpr int ID_LEN = 4;                      // uint32_t,请求/回复用来配对
    static constexpr int HEAD_LEN = 2;                    // body 长度
    static constexpr int PREFIX_LEN = ID_LEN + HEAD_LEN;  // 头部总长度,先读这么多字节

    MsgNode() = default;
    MsgNode(uint32_t id, const char* body) : id_(id) {
        // TODO 检查body_length
        if (body) {
            set_body_length(strlen(body));
            encode_header();
            memcpy(data_ + PREFIX_LEN, body, body_length_);
        } else {
            encode_header();
        }
    }
    MsgNode(uint32_t id, std::string&& body) : MsgNode(id, body.c_str()) {}

    const char* data() const {
        return data_;
    }

    char* data() {
        return data_;
    }

    const char* body() const {
        return data_ + PREFIX_LEN;
    }

    char* body() {
        return data_ + PREFIX_LEN;
    }

    int body_length() const {
        return body_length_;
    }

    // 整条消息(含头部)的总长度
    int length() const {
        return PREFIX_LEN + body_length_;
    }

    uint32_t id() const {
        return id_;
    }

    void set_id(uint32_t id) {
        id_ = id;
    }

    bool set_body_length(int length) {
        if (length > static_cast<int>(MAX_LENGTH)) {
            return false;
        }
        body_length_ = length;
        return true;
    }

    void clear() {
        ::memset(data_, 0, body_length_);
    }

    // 从 data_ 开头解析 id 和 body_length(要求调用前,data_ 前 PREFIX_LEN 字节已经填好)
    bool decode_header() {
        std::memcpy(&id_, data_, ID_LEN);
        uint16_t len = 0;
        std::memcpy(&len, data_ + ID_LEN, HEAD_LEN);
        // len = ntohs(len);
        return set_body_length(len);
    }

    // 把 id_/body_length_ 写进 data_ 开头(调用前先 set_id()/set_body_length() 设置好这两个字段)
    void encode_header() {
        std::memcpy(data_, &id_, ID_LEN);
        std::memcpy(data_ + ID_LEN, &body_length_, HEAD_LEN);
    }

private:
    uint32_t id_ = 0;
    int body_length_ = 0;
    char data_[PREFIX_LEN + MAX_LENGTH + 1] = {0};
};

#endif