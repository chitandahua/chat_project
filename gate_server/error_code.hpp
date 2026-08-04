#ifndef _ERROR_CODE_HPP_
#define _ERROR_CODE_HPP_

#include <cstdint>

enum class ErrorCode : uint8_t {
    SUCCESS = 0,
    FAILED,
    NOT_FOUND,
    TIMEOUT,
    INVALID_JSON,
    RPC_FAILED,
    INVALID_VERIFY_CODE,
    USER_OR_EMAIL_EXIST,
    UNKNOWN,
};

#endif