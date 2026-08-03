#ifndef _ERROR_CODE_HPP_
#define _ERROR_CODE_HPP_

#include <cstdint>

enum class ErrorCode : uint8_t {
    SUCCESS = 0,
    FAILED,
    NOT_FOUND,
    TIMEOUT,
    UNKNOWN,
};

#endif