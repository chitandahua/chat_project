
#ifndef _ERROR_HPP_
#define _ERROR_HPP_

#include <boost/system/error_category.hpp>

#include <mutex>
#include <string_view>
#include <type_traits>

#include <cstdint>

enum class ServiceError : uint8_t {
    SUCCESS = 0,
    NOT_FOUND = 1,
    INVALID_JSON = 3,
    RPC_FAILED = 4,
    INVALID_VERIFY_CODE = 5,
    USER_OR_EMAIL_EXIST = 6,
};

enum class ErrorCode : uint8_t {
    NOT_FOUND = 0,
    USER_OR_EMAIL_EXIST,
    UNKNOWN,
};

// To use errc with boost::system::error_code, we need
// to define an error category (see the cpp file).
const boost::system::error_category& get_service_category();

// Called when constructing an error_code from an errc value.
inline boost::system::error_code make_error_code(ErrorCode v) {
    return boost::system::error_code(static_cast<int>(v), get_service_category());
}

// In multi-threaded programs, using std::cerr without any locking
// can result in interleaved output.
// Locks a mutex guarding std::cerr to prevent this.
// All uses of std::cerr should respect this.
std::unique_lock<std::mutex> lock_cerr();

void log_error(std::string_view header, boost::system::error_code ec);

// This specialization is required to construct error_code's from errc values
template <>
struct boost::system::is_error_code_enum<ErrorCode> : std::true_type {};

#endif