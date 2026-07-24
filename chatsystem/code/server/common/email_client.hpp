// =============================================================================
// email_client.hpp - 邮件发送客户端封装模块
// =============================================================================
// 基于 SMTP 协议封装的邮件发送客户端，用于向用户邮箱发送验证码。
// 手动实现 SMTP 协议（STARTTLS + AUTH PLAIN），通过 QQ 邮箱 SMTP 服务器发送。
//
// 依赖说明：
//   - OpenSSL: SSL/TLS 加密通信
//   - spdlog: 日志库（通过 log.hpp）
// =============================================================================

#ifndef EMAIL_CLIENT_HPP
#define EMAIL_CLIENT_HPP

#include <string>
#include <random>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "log.hpp"

namespace email_client {

/**
 * @brief 邮件发送客户端类
 *
 * 封装 SMTP 协议，通过 QQ 邮箱 SMTP 服务器发送验证码邮件。
 * 支持 STARTTLS 加密传输和 AUTH PLAIN 认证。
 */
class EmailClient {
public:
    /**
     * @brief 构造函数
     * @param smtp_server SMTP 服务器地址，默认 "smtp.qq.com"
     * @param smtp_port SMTP 服务器端口，默认 587
     * @param sender_email 发件人邮箱地址
     * @param auth_code SMTP 授权码（非邮箱登录密码）
     */
    EmailClient(const std::string& smtp_server = "smtp.qq.com",
                int smtp_port = 587,
                const std::string& sender_email = "",
                const std::string& auth_code = "")
        : smtp_server_(smtp_server)
        , smtp_port_(smtp_port)
        , sender_email_(sender_email)
        , auth_code_(auth_code) {}

    /**
     * @brief 设置发件人信息
     * @param email 发件人邮箱地址
     * @param auth_code SMTP 授权码
     */
    void set_sender(const std::string& email, const std::string& auth_code) {
        sender_email_ = email;
        auth_code_ = auth_code;
    }

    /**
     * @brief 发送验证码邮件
     * @param to_email 收件人邮箱地址
     * @param verify_code 验证码
     * @return 发送成功返回 true，失败返回 false
     */
    bool send_verify_code(const std::string& to_email, const std::string& verify_code) {
        if (sender_email_.empty() || auth_code_.empty()) {
            LOG_ERROR("[EmailClient] Sender email or auth code not configured");
            return false;
        }

        int sock = -1;
        SSL* ssl = nullptr;
        SSL_CTX* ctx = nullptr;

        // RAII 清理助手
        auto cleanup = [&]() {
            if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
            if (ctx) { SSL_CTX_free(ctx); }
            if (sock >= 0) { close(sock); }
        };

        // ---------- 1. TCP 连接 ----------
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_ERROR("[EmailClient] Create socket failed");
            return false;
        }

        struct hostent* host = gethostbyname(smtp_server_.c_str());
        if (!host) {
            LOG_ERROR("[EmailClient] DNS resolve failed: {}", smtp_server_);
            cleanup();
            return false;
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(smtp_port_);
        memcpy(&addr.sin_addr, host->h_addr, host->h_length);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_ERROR("[EmailClient] TCP connect failed to {}:{}", smtp_server_, smtp_port_);
            cleanup();
            return false;
        }

        // ---------- 2. 读取 greeting ----------
        int code = smtp_cmd(nullptr, sock, "");
        if (code != 220) {
            LOG_ERROR("[EmailClient] SMTP greeting error: {}", code);
            cleanup();
            return false;
        }

        // ---------- 3. EHLO ----------
        code = smtp_cmd(nullptr, sock, "EHLO localhost");
        if (code != 250) {
            LOG_ERROR("[EmailClient] EHLO failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 4. STARTTLS ----------
        code = smtp_cmd(nullptr, sock, "STARTTLS");
        if (code != 220) {
            LOG_ERROR("[EmailClient] STARTTLS failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 5. SSL 握手 ----------
        SSL_library_init();
        SSL_load_error_strings();
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            LOG_ERROR("[EmailClient] SSL_CTX_new failed");
            cleanup();
            return false;
        }

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) != 1) {
            LOG_ERROR("[EmailClient] SSL handshake failed");
            cleanup();
            return false;
        }

        // ---------- 6. EHLO (over TLS) ----------
        code = smtp_cmd(ssl, -1, "EHLO localhost");
        if (code != 250) {
            LOG_ERROR("[EmailClient] EHLO (TLS) failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 7. AUTH PLAIN (inline) ----------
        {
            std::string auth_raw = std::string(1, '\0') + sender_email_ + std::string(1, '\0') + auth_code_;
            std::string auth_b64 = base64_encode(auth_raw);
            std::string auth_cmd = "AUTH PLAIN " + auth_b64;
            code = smtp_cmd(ssl, -1, auth_cmd);
        }
        if (code != 235) {
            LOG_ERROR("[EmailClient] AUTH failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 8. MAIL FROM ----------
        code = smtp_cmd(ssl, -1, "MAIL FROM:<" + sender_email_ + ">");
        if (code != 250) {
            LOG_ERROR("[EmailClient] MAIL FROM failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 9. RCPT TO ----------
        code = smtp_cmd(ssl, -1, "RCPT TO:<" + to_email + ">");
        if (code != 250) {
            LOG_ERROR("[EmailClient] RCPT TO failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 10. DATA ----------
        code = smtp_cmd(ssl, -1, "DATA");
        if (code != 354) {
            LOG_ERROR("[EmailClient] DATA failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 11. 发送邮件内容 ----------
        {
            std::ostringstream mail;
            mail << "From: " << sender_email_ << "\r\n"
                 << "To: " << to_email << "\r\n"
                 << "Subject: =?UTF-8?B?" << base64_encode("聊天系统验证码") << "?=\r\n"
                 << "MIME-Version: 1.0\r\n"
                 << "Content-Type: text/plain; charset=UTF-8\r\n"
                 << "Content-Transfer-Encoding: base64\r\n"
                 << "\r\n";

            std::string body_text = std::string("您的验证码是: ") + verify_code
                + "\n验证码5分钟内有效，请勿泄露给他人。\n\n"
                + "如果这不是您本人的操作，请忽略此邮件。";
            mail << base64_encode(body_text) << "\r\n.\r\n";

            std::string mail_str = mail.str();
            SSL_write(ssl, mail_str.c_str(), mail_str.size());
            std::string resp = recv_line(ssl, -1);
            code = resp.empty() ? -1 : std::stoi(resp.substr(0, 3));
        }
        if (code != 250) {
            LOG_ERROR("[EmailClient] Mail send failed: {}", code);
            cleanup();
            return false;
        }

        // ---------- 12. QUIT ----------
        smtp_cmd(ssl, -1, "QUIT");
        LOG_INFO("[EmailClient] Verify code sent to {}", to_email);
        cleanup();
        return true;
    }

    /**
     * @brief 生成随机验证码
     * @param length 验证码长度，默认 4 位
     * @return 随机数字验证码字符串
     */
    static std::string generate_code(int length = 4) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 9);
        std::ostringstream oss;
        for (int i = 0; i < length; ++i) {
            oss << dist(gen);
        }
        return oss.str();
    }

private:
    /**
     * @brief Base64 编码
     */
    static std::string base64_encode(const std::string& input) {
        static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::ostringstream out;
        int val = 0, valb = -6;
        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out << tbl[(val >> valb) & 0x3F];
                valb -= 6;
            }
        }
        if (valb > -6) out << tbl[((val << 8) >> (valb + 8)) & 0x3F];
        while (out.str().size() % 4) out << '=';
        return out.str();
    }

    /**
     * @brief 读取一行 SMTP 响应
     */
    static std::string recv_line(SSL* ssl, int sock) {
        std::string line;
        char c;
        while (true) {
            int n = ssl ? SSL_read(ssl, &c, 1) : recv(sock, &c, 1, 0);
            if (n <= 0) return "";
            line += c;
            if (c == '\n') break;
        }
        return line;
    }

    /**
     * @brief 发送 SMTP 命令并读取响应
     * @return SMTP 响应码
     */
    static int smtp_cmd(SSL* ssl, int sock, const std::string& cmd) {
        if (!cmd.empty()) {
            std::string s = cmd + "\r\n";
            if (ssl)
                SSL_write(ssl, s.c_str(), s.size());
            else
                send(sock, s.c_str(), s.size(), 0);
        }
        int code = 0;
        while (true) {
            std::string line = recv_line(ssl, sock);
            if (line.empty()) return -1;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (line.length() >= 4) code = std::stoi(line.substr(0, 3));
            if (line.length() >= 4 && line[3] == ' ') break;
        }
        return code;
    }

    std::string smtp_server_;     ///< SMTP 服务器地址
    int smtp_port_;               ///< SMTP 服务器端口
    std::string sender_email_;    ///< 发件人邮箱
    std::string auth_code_;       ///< SMTP 授权码
};

} // namespace email_client

#endif // EMAIL_CLIENT_HPP
