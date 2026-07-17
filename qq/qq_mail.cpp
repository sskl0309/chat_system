// QQ邮箱SMTP发送验证码示例
// 手动实现 SMTP 协议（STARTTLS + AUTH PLAIN inline），绕过 libcurl 的分步认证问题

#include <iostream>
#include <string>
#include <random>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// ==================== 邮箱配置 ====================
const std::string SMTP_SERVER  = "smtp.qq.com";
const int         SMTP_PORT    = 587;
const std::string SENDER_EMAIL = "3502173090@qq.com";
const std::string AUTH_CODE    = "ilymtchfymaychdb";

// ==================== 工具函数 ====================

/// 生成 N 位随机数字验证码
std::string generate_code(int length = 6) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 9);
    std::ostringstream oss;
    for (int i = 0; i < length; ++i) oss << dist(gen);
    return oss.str();
}

/// Base64 编码（RFC 2045）
std::string base64_encode(const std::string& input) {
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

/// 读取一行 SMTP 响应
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

/// 发送 SMTP 命令并读取响应，返回响应码(如 250, 235, 334)
static int smtp_cmd(SSL* ssl, int sock, const std::string& cmd) {
    if (!cmd.empty()) {
        std::string s = cmd + "\r\n";
        // 打印发送的命令（不打印完整 base64 避免太长）
        std::string preview = s;
        if (preview.size() > 80) preview = preview.substr(0, 77) + "...";
        std::cout << "> " << preview;
        if (ssl)
            SSL_write(ssl, s.c_str(), s.size());
        else
            send(sock, s.c_str(), s.size(), 0);
    }
    std::string resp;
    int code = 0;
    while (true) {
        std::string line = recv_line(ssl, sock);
        if (line.empty()) return -1;
        // 去掉 \r\n
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::cout << "< " << line << std::endl;
        // SMTP 多行响应的最后一行以空格分隔（如 "250 OK"），中间行以 '-' 分隔（如 "250-PIPELINING"）
        if (line.length() >= 4) code = std::stoi(line.substr(0, 3));
        if (line.length() >= 4 && line[3] == ' ') break; // 最后一行
    }
    return code;
}

// ==================== 邮件发送 ====================

bool send_mail(const std::string& to_email, const std::string& vcode) {
    // ---------- 1. TCP 连接 ----------
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { std::cerr << "[错误] 创建 socket 失败" << std::endl; return false; }

    struct hostent* host = gethostbyname(SMTP_SERVER.c_str());
    if (!host) { std::cerr << "[错误] DNS 解析失败" << std::endl; close(sock); return false; }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SMTP_PORT);
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[错误] TCP 连接失败" << std::endl; close(sock); return false;
    }
    std::cout << "[连接] 已连接到 " << SMTP_SERVER << ":" << SMTP_PORT << std::endl;

    // ---------- 2. 读取 greeting ----------
    int code = smtp_cmd(nullptr, sock, "");
    if (code != 220) { std::cerr << "[错误] SMTP greeting 异常: " << code << std::endl; close(sock); return false; }

    // ---------- 3. EHLO ----------
    code = smtp_cmd(nullptr, sock, "EHLO localhost");
    if (code != 250) { std::cerr << "[错误] EHLO 失败: " << code << std::endl; close(sock); return false; }

    // ---------- 4. STARTTLS ----------
    code = smtp_cmd(nullptr, sock, "STARTTLS");
    if (code != 220) { std::cerr << "[错误] STARTTLS 失败: " << code << std::endl; close(sock); return false; }

    // ---------- 5. SSL 握手 ----------
    SSL_library_init();
    SSL_load_error_strings();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { std::cerr << "[错误] SSL_CTX_new 失败" << std::endl; close(sock); return false; }

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) != 1) {
        std::cerr << "[错误] SSL 握手失败" << std::endl;
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock); return false;
    }
    std::cout << "[TLS] 安全连接已建立" << std::endl;

    // ---------- 6. EHLO (over TLS) ----------
    code = smtp_cmd(ssl, -1, "EHLO localhost");
    if (code != 250) { std::cerr << "[错误] EHLO (TLS) 失败: " << code << std::endl; goto fail; }

    // ---------- 7. AUTH PLAIN (inline) ----------
    // 关键：必须一行发送 "AUTH PLAIN <base64>"，QQ SMTP 不接受分步发送
    {
        std::string auth_raw = std::string(1, '\0') + SENDER_EMAIL + std::string(1, '\0') + AUTH_CODE;
        std::string auth_b64 = base64_encode(auth_raw);
        std::string auth_cmd = "AUTH PLAIN " + auth_b64;
        code = smtp_cmd(ssl, -1, auth_cmd);
    }
    if (code != 235) { std::cerr << "[错误] 认证失败: " << code << std::endl; goto fail; }
    std::cout << "[认证] 登录成功" << std::endl;

    // ---------- 8. MAIL FROM ----------
    code = smtp_cmd(ssl, -1, "MAIL FROM:<" + SENDER_EMAIL + ">");
    if (code != 250) { std::cerr << "[错误] MAIL FROM 失败: " << code << std::endl; goto fail; }

    // ---------- 9. RCPT TO ----------
    code = smtp_cmd(ssl, -1, "RCPT TO:<" + to_email + ">");
    if (code != 250) { std::cerr << "[错误] RCPT TO 失败: " << code << std::endl; goto fail; }

    // ---------- 10. DATA ----------
    code = smtp_cmd(ssl, -1, "DATA");
    if (code != 354) { std::cerr << "[错误] DATA 失败: " << code << std::endl; goto fail; }

    // ---------- 11. 发送邮件内容 ----------
    {
        std::ostringstream mail;
        mail << "From: " << SENDER_EMAIL << "\r\n"
             << "To: " << to_email << "\r\n"
             << "Subject: =?UTF-8?B?" << base64_encode("验证码") << "?=\r\n"
             << "MIME-Version: 1.0\r\n"
             << "Content-Type: text/plain; charset=UTF-8\r\n"
             << "Content-Transfer-Encoding: base64\r\n"
             << "\r\n";

        std::string body_text = std::string("您的验证码是: ") + vcode
            + "\n验证码5分钟内有效，请勿泄露给他人。\n\n"
            + "如果这不是您本人的操作，请忽略此邮件。";
        mail << base64_encode(body_text) << "\r\n.\r\n";

        std::string mail_str = mail.str();
        SSL_write(ssl, mail_str.c_str(), mail_str.size());
        std::string resp = recv_line(ssl, -1);
        code = resp.empty() ? -1 : std::stoi(resp.substr(0, 3));
    }
    if (code != 250) { std::cerr << "[错误] 邮件发送失败: " << code << std::endl; goto fail; }

    // ---------- 12. QUIT ----------
    smtp_cmd(ssl, -1, "QUIT");
    std::cout << "[成功] 验证码已发送!" << std::endl;

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    return true;

fail:
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    return false;
}

// ==================== 主函数 ====================

int main() {
    std::string code = generate_code(6);
    std::cout << "[生成] 验证码: " << code << std::endl;

    std::string target_email = "3049075877@qq.com";
    std::cout << "[信息] 正在发送验证码到 " << target_email << " ..." << std::endl;

    bool ok = send_mail(target_email, code);

    if (ok) {
        std::cout << "\n==============================" << std::endl;
        std::cout << "  验证码 " << code << " 已发送至 " << target_email << std::endl;
        std::cout << "==============================" << std::endl;
    }

    return ok ? 0 : 1;
}
