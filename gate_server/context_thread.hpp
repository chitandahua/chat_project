#ifndef _CONTEXT_THREAD_HPP_
#define _CONTEXT_THREAD_HPP_

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>

#include <atomic>
#include <memory>
#include <thread>

class ContextThread {
public:
    ContextThread()
        : io_context_(std::make_unique<boost::asio::io_context>()),
          work_guard_(boost::asio::make_work_guard(*io_context_)) {}
    ContextThread(ContextThread&) = delete;
    ContextThread& operator=(ContextThread&) = delete;
    ContextThread(ContextThread&&) = delete;
    ContextThread& operator=(ContextThread&&) = delete;
    ~ContextThread() {
        stop();
    }

    void run() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        thread_ = std::thread([this] { io_context_->run(); });
    }

    void stop() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }

        work_guard_.reset();
        join();
    }

    boost::asio::io_context& get_io_context() {
        return *io_context_;
    }

private:
    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::unique_ptr<boost::asio::io_context> io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::thread thread_;
    std::atomic<bool> running_ = false;
};

#endif