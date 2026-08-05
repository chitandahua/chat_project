

#include <boost/asio/awaitable.hpp>
#include <boost/pfr/config.hpp>

#include <boost/system/error_category.hpp>

#include <iostream>
#include <mutex>

#include "error.hpp"

namespace {

const char* error_to_string(ErrorCode value) {
    switch (value) {
        case ErrorCode::NOT_FOUND:
            return "not_found";
        case ErrorCode::USER_OR_EMAIL_EXIST:
            return "user_or_email_exist";
        default:
            return "<unknown ErrorCode>";
    }
}

// The category to be returned by get_service_category
class service_category final : public boost::system::error_category {
public:
    const char* name() const noexcept final override {
        return "service";
    }

    std::string message(int ev) const final override {
        return error_to_string(static_cast<ErrorCode>(ev));
    }
};

// The error category
const service_category g_category;

std::mutex g_cerr_mutex;
}  // namespace

const boost::system::error_category& get_service_category() {
    return g_category;
}

std::unique_lock<std::mutex> lock_cerr() {
    return std::unique_lock{g_cerr_mutex};
}

void log_error(std::string_view header, boost::system::error_code ec) {
    // Lock the mutex
    auto guard = lock_cerr();

    // Logging the error code prints the number and category. Add the message, too
    std::cerr << header << ": " << ec << " " << ec.message() << std::endl;
}
