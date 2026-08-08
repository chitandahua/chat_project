#ifndef _MAIL_CLIENT_HPP_
#define _MAIL_CLIENT_HPP_

#include <cstdio>
#include <string>

// 用系统 sendmail(/usr/bin/mail)发信,通过 fork 外部进程完成投递。
// 不用 mailio 库:mailio 0.26 的 dialog::connect 在 gRPC 已初始化的进程里
// (verify_server)会段错误,而独立测试正常——库与 gRPC/OpenSSL 共存时不稳定。
class MailClient {
public:
    MailClient(const std::string& host, int port) : smtp_host_(host), smtp_port_(port) {}

    int send_mail(const std::string& to, const std::string& subject, const std::string& body) {
        // 本地场景:系统 MTA(OpenSMTPD)固定投递,host/port 不再使用
        (void)smtp_host_;
        (void)smtp_port_;

        std::string cmd =
            "/usr/bin/mail -s " + shell_quote(subject) + " " + shell_quote(to) + " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            fprintf(stderr, "[mail] popen failed for %s\n", to.c_str());
            return -1;
        }
        fwrite(body.c_str(), 1, body.size(), pipe);
        int rc = pclose(pipe);
        if (rc != 0) {
            fprintf(stderr, "[mail] send failed to %s rc=%d\n", to.c_str(), rc);
            return -1;
        }
        fprintf(stderr, "[mail] ok to %s via sendmail\n", to.c_str());
        return 0;
    }

private:
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

    std::string smtp_host_;
    int smtp_port_;
};

#endif
