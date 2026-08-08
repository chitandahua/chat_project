#ifndef _MAIL_CLIENT_HPP_
#define _MAIL_CLIENT_HPP_

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

// 邮件投递:单一常驻 worker 线程 + 任务队列。
// send_mail() 只把任务入队立即返回,绝不阻塞调用者(尤其不能阻塞 verify 的
// io_context——gRPC 回调跑在上面)。实际投递由 worker 线程串行执行,通过
// fork 系统 sendmail(/usr/bin/mail)完成,不使用 mailio(mailio 0.26 在
// gRPC 已初始化的进程里 dialog::connect 会段错误)。
class MailClient {
public:
    static MailClient& instance() {
        static MailClient mc;
        return mc;
    }

    // 入队一封邮件,立即返回。线程安全。
    void send_mail(const std::string& to, const std::string& subject, const std::string& body) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            queue_.push_back({to, subject, body});
        }
        cv_.notify_one();
    }

private:
    struct Task {
        std::string to;
        std::string subject;
        std::string body;
    };

    MailClient() {
        worker_ = std::thread([this] { worker_loop(); });
    }

    ~MailClient() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    MailClient(const MailClient&) = delete;
    MailClient& operator=(const MailClient&) = delete;

    void worker_loop() {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            deliver(task);
        }
    }

    void deliver(const Task& task) {
        std::string cmd =
            "/usr/bin/mail -s " + shell_quote(task.subject) + " " + shell_quote(task.to) +
            " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            fprintf(stderr, "[mail] popen failed for %s\n", task.to.c_str());
            return;
        }
        fwrite(task.body.c_str(), 1, task.body.size(), pipe);
        int rc = pclose(pipe);
        if (rc != 0) {
            fprintf(stderr, "[mail] send failed to %s rc=%d\n", task.to.c_str(), rc);
            return;
        }
        fprintf(stderr, "[mail] ok to %s via sendmail\n", task.to.c_str());
    }

    static std::string shell_quote(const std::string& s) {
        // 单引号包裹,内部的单引号转义为 '\'' 序列,防止 shell 注入
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += "'";
        return out;
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    bool stop_ = false;
    std::thread worker_;
};

#endif
