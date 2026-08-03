#ifndef _CONTEXT_POOL_HPP_
#define _CONTEXT_POOL_HPP_

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include "context_thread.hpp"

class ContextPool {
public:
    ContextPool(size_t size) {
        for (size_t i = 0; i < size; ++i) {
            context_threads_.push_back(std::make_unique<ContextThread>());
        }
    }

    boost::asio::io_context& get_io_context() {
        // 轮询取一个 io_context,做简单的负载均衡
        auto& io_context = context_threads_[next_io_context_]->get_io_context();
        next_io_context_ = (next_io_context_ + 1) % context_threads_.size();
        return io_context;
    }

    void run() {
        for (auto& context_thread : context_threads_) {
            context_thread->run();
        }
    }

    void stop() {
        for (auto& context_thread : context_threads_) {
            context_thread->stop();
        }
    }

private:
    // boost::asio::io_context& io_context_;
    std::vector<std::unique_ptr<ContextThread>> context_threads_;
    std::size_t next_io_context_ = 0;
};

#endif