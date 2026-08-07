#ifndef _MAIL_CLIENT_HPP_
#define _MAIL_CLIENT_HPP_

#include <iostream>
#include <mailio/message.hpp>
#include <mailio/smtp.hpp>
#include <string>

using mailio::dialog_error;
using mailio::mail_address;
using mailio::smtp;
using mailio::smtp_error;

class MailClient {
public:
    MailClient(const std::string& host, int port) : smtp_host_(host), smtp_port_(port) {}

    int send_mail(const std::string& to, const std::string& subject, const std::string& body) {
        try {
            // create mail message
            mailio::message msg;
            msg.from(mail_address("mailio library",
                                  "mailio@gmail.com"));  // set the correct sender name and address
            msg.add_recipient(mail_address("",
                                           to));  // set the correct recipent name and address
            msg.subject(subject);
            msg.content(body);

            // connect to server
            smtp conn(smtp_host_, smtp_port_);
            // modify username/password to use real credentials
            // conn.authenticate("mailio@gmail.com", "mailiopass", smtp::auth_method_t::LOGIN);
            conn.submit(msg);
        } catch (smtp_error& exc) {
            std::cout << exc.what() << "\n";
            return -1;
        } catch (dialog_error& exc) {
            std::cout << exc.what() << "\n";
            return -1;
        }

        return 0;
    }

private:
    std::string smtp_host_;
    int smtp_port_;
};

#endif