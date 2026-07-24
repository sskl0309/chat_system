// =============================================================================
// email_client_test.cc - 邮件客户端 gtest 测试
// =============================================================================
// 基于 Google Test 框架测试 common/email_client.hpp 的全部功能：
//   1. generate_code          - 验证码生成（默认长度 / 自定义长度 / 唯一性）
//   2. 构造函数               - 默认构造与带参构造
//   3. set_sender             - 设置发件人信息
//   4. send_verify_code       - 端到端 SMTP 发送（需要真实授权码）
//   5. 未配置发件人时发送失败 - 边界条件
//
// 测试策略：
//   - 纯逻辑测试（验证码生成）：无需外部依赖，直接断言
//   - SMTP 发送测试：通过 gflags 注入 QQ 邮箱地址与授权码，
//     未提供凭据时使用 GTEST_SKIP 跳过，避免 CI 误报
//
// 运行前提：
//   - 网络可访问 smtp.qq.com:587
//   - （可选）通过 --sender_email / --auth_code 提供真实凭据以执行发送测试
//
// 运行方式：
//   ./email_client_test
//   ./email_client_test --sender_email=xxx@qq.com --auth_code=YYYYYYYY
//                     --recipient_email=yyy@example.com
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <string>
#include <memory>
#include <regex>
#include <set>

#include "email_client.hpp"
#include "log.hpp"

// ==================== gflags 命令行参数定义 ====================

// QQ 邮箱 SMTP 发件人地址（需与授权码配对）
DEFINE_string(sender_email, "", "Sender QQ email address for SMTP AUTH");
// SMTP 授权码（非邮箱登录密码，需在 QQ 邮箱后台开启 SMTP 服务后获取）
DEFINE_string(auth_code, "", "SMTP authorization code for QQ mailbox");
// 测试收件人邮箱地址（可以是任意邮箱，用于接收验证码邮件）
DEFINE_string(recipient_email, "", "Recipient email address to receive test verify code");
// SMTP 服务器地址
DEFINE_string(smtp_server, "smtp.qq.com", "SMTP server address");
// SMTP 服务器端口
DEFINE_int32(smtp_port, 587, "SMTP server port");

// ==================== 测试夹具 ====================

/**
 * @brief 邮件客户端测试夹具
 *
 * 管理日志初始化与 EmailClient 实例的创建。
 * 所有测试用例共享同一份日志配置，EmailClient 按需构造以隔离状态。
 */
class EmailClientTest : public ::testing::Test {
protected:
    /**
     * @brief 测试前置：初始化日志系统
     */
    void SetUp() override {
        // debug 模式输出彩色日志到控制台，便于调试
        mylog::init(true, "", mylog::LogLevel::INFO);
    }

    /**
     * @brief 构造一个使用 gflags 参数的 EmailClient
     *
     * 用于端到端 SMTP 发送测试，调用方需自行检查 FLAGS_sender_email / FLAGS_auth_code 是否为空。
     *
     * @return std::unique_ptr<email_client::EmailClient>
     */
    std::unique_ptr<email_client::EmailClient> make_configured_client() {
        return std::make_unique<email_client::EmailClient>(
            FLAGS_smtp_server, FLAGS_smtp_port, FLAGS_sender_email, FLAGS_auth_code);
    }
};

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：generate_code - 默认长度为 4 位且全为数字
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, GenerateCodeDefaultLength) {
    std::string code = email_client::EmailClient::generate_code();

    // 验证默认长度为 4
    EXPECT_EQ(code.size(), 4u)
        << "Default verify code length should be 4, got: " << code;

    // 验证全部为数字字符
    for (char c : code) {
        EXPECT_TRUE(c >= '0' && c <= '9')
            << "Verify code should contain only digits, found: " << c;
    }
}

// ---------------------------------------------------------------------------
// 测试 2：generate_code - 自定义长度
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, GenerateCodeCustomLength) {
    for (int len : {1, 6, 8, 10}) {
        std::string code = email_client::EmailClient::generate_code(len);
        EXPECT_EQ(static_cast<int>(code.size()), len)
            << "Verify code length mismatch for requested length " << len;
    }
}

// ---------------------------------------------------------------------------
// 测试 3：generate_code - 多次调用应当产生不同的验证码（随机性）
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, GenerateCodeUniqueness) {
    const int sample_count = 100;
    std::set<std::string> codes;

    for (int i = 0; i < sample_count; ++i) {
        codes.insert(email_client::EmailClient::generate_code());
    }

    // 随机性检查：100 次调用中应至少产生 50 个不同的验证码
    // （允许碰撞，但不应过于集中）
    EXPECT_GT(static_cast<int>(codes.size()), 50)
        << "Generated codes lack randomness, unique count: " << codes.size();
}

// ---------------------------------------------------------------------------
// 测试 4：默认构造 - 未配置发件人时 send_verify_code 应失败
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, SendWithoutSenderFails) {
    email_client::EmailClient client;  // 默认构造，sender_email_ 和 auth_code_ 均为空

    bool ok = client.send_verify_code("recipient@example.com", "1234");
    EXPECT_FALSE(ok) << "send_verify_code should fail when sender is not configured";
}

// ---------------------------------------------------------------------------
// 测试 5：set_sender - 设置发件人后内部状态变更
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, SetSenderUpdatesState) {
    email_client::EmailClient client;

    // set_sender 后，仍缺少授权码的情况下（这里两个都给了空字符串）
    // 内部状态变化无法直接观察，仅验证不抛异常
    EXPECT_NO_THROW(client.set_sender("", ""));

    // 设置真实凭据后，发送给无效地址应能进入 SMTP 流程
    // （此处只验证 set_sender 不影响对象完整性）
    client.set_sender("test_sender@qq.com", "test_auth_code");
    bool ok = client.send_verify_code("invalid_address", "1234");
    // 无效地址或假授权码必然失败，验证函数被正确调用
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// 测试 6：构造函数 - 带 SMTP 参数构造
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, ConstructWithSmtpParams) {
    email_client::EmailClient client("smtp.qq.com", 587, "sender@qq.com", "authcode");

    // 构造后立即发送给无效地址，应失败但不应崩溃
    bool ok = client.send_verify_code("bad_address", "0000");
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// 测试 7：send_verify_code - 端到端 SMTP 发送
// ---------------------------------------------------------------------------
// 本测试需要真实的 QQ 邮箱凭据，未提供时跳过，避免误报失败。
TEST_F(EmailClientTest, SendVerifyCodeEndToEnd) {
    if (FLAGS_sender_email.empty() || FLAGS_auth_code.empty()
        || FLAGS_recipient_email.empty()) {
        GTEST_SKIP() << "Skip end-to-end SMTP test: "
                     << "please provide --sender_email, --auth_code, --recipient_email";
    }

    auto client = make_configured_client();
    ASSERT_NE(client, nullptr);

    std::string code = email_client::EmailClient::generate_code(6);
    bool ok = client->send_verify_code(FLAGS_recipient_email, code);

    // 网络环境正常 + 凭据正确时应成功
    EXPECT_TRUE(ok) << "Failed to send verify code to " << FLAGS_recipient_email
                    << ", please check credentials / network";

    LOG_INFO("[EmailClientTest] Verify code {} sent to {}", code, FLAGS_recipient_email);
}

// ---------------------------------------------------------------------------
// 测试 8：send_verify_code - TTL 文案与 6 位验证码组合
// ---------------------------------------------------------------------------
TEST_F(EmailClientTest, SendVerifyCodeWithSixDigitCode) {
    if (FLAGS_sender_email.empty() || FLAGS_auth_code.empty()
        || FLAGS_recipient_email.empty()) {
        GTEST_SKIP() << "Skip end-to-end SMTP test: "
                     << "please provide --sender_email, --auth_code, --recipient_email";
    }

    auto client = make_configured_client();
    std::string code = email_client::EmailClient::generate_code(6);
    ASSERT_EQ(code.size(), 6u);

    bool ok = client->send_verify_code(FLAGS_recipient_email, code);
    EXPECT_TRUE(ok);
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    // 解析 gflags 和 gtest 参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
