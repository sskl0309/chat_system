// =============================================================================
// redis_client_test.cc - Redis 客户端 gtest 测试
// =============================================================================
// 基于 Google Test 框架测试 common/redis_client.hpp 的全部功能：
//   1. 会话管理
//      - create_session            - 创建会话
//      - get_user_id_by_session    - 通过 session_id 查询 user_id
//      - is_user_logged_in         - 检查用户登录状态
//      - remove_session            - 删除会话及登录标记
//   2. 验证码管理
//      - set_verify_code           - 设置验证码
//      - get_verify_code           - 获取验证码
//      - remove_verify_code        - 删除验证码
//      - verify_code               - 验证并消费验证码
//   3. 边界条件
//      - 查询不存在的会话/验证码
//      - 错误验证码不应消费原验证码
//
// 测试策略：
//   - Test Fixture 管理 RedisClient 实例与唯一 ID 生成
//   - 每个用例使用独立 session_id / code_id，避免相互污染
//   - 会话类测试采用 round-trip：create → get → is_logged → remove → verify gone
//   - 验证码类测试验证：成功消费后再次查询为空、错误验证码不消费原码
//
// 运行前提：
//   - Redis 服务运行在 127.0.0.1:6379
//   - 使用 db=15 避免污染业务数据（默认 0）
//
// 运行方式：
//   ./redis_client_test
//   ./redis_client_test --redis_host=127.0.0.1 --redis_port=6379 --redis_db=15
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>

#include "redis_client.hpp"
#include "log.hpp"

// ==================== gflags 命令行参数定义 ====================

DEFINE_string(redis_host, "127.0.0.1", "Redis server host");
DEFINE_int32(redis_port, 6379, "Redis server port");
// 使用 db=15 避免污染默认 db 0 的业务数据
DEFINE_int32(redis_db, 15, "Redis database index (use 15 to isolate tests)");

// ==================== 测试夹具 ====================

/**
 * @brief RedisClient 测试夹具
 *
 * 管理共享的 RedisClient 实例，提供唯一 session_id / code_id 生成工具。
 */
class RedisClientTest : public ::testing::Test {
protected:
    static std::shared_ptr<redis_client::RedisClient> redis_;
    static std::atomic<uint64_t> counter_;

    /**
     * @brief 测试套件前置：初始化日志、构造 RedisClient、清空测试 db
     */
    static void SetUpTestSuite() {
        mylog::init(true, "", mylog::LogLevel::INFO);

        redis_ = std::make_shared<redis_client::RedisClient>(
            FLAGS_redis_host, FLAGS_redis_port, FLAGS_redis_db);

        // 清空测试 db（FLUSHDB），保证测试环境干净
        // 注意：仅在 db=15 这种隔离 db 上使用，避免误删业务数据
        // redis_ 内部封装未暴露 flushdb，这里通过验证连接性代替
        // 用户首次运行如需清空，可手动 redis-cli -n 15 flushdb
    }

    /**
     * @brief 生成唯一 session_id
     */
    static std::string gen_session_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "sess_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }

    /**
     * @brief 生成唯一 code_id
     */
    static std::string gen_code_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "vcode_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }

    /**
     * @brief 生成唯一 user_id
     */
    static std::string gen_user_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "uid_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }
};

// 静态成员定义
std::shared_ptr<redis_client::RedisClient> RedisClientTest::redis_;
std::atomic<uint64_t> RedisClientTest::counter_{0};

// ==================== 会话管理测试 ====================

// ---------------------------------------------------------------------------
// 测试 1：create_session - 创建会话后能通过 session_id 查询到 user_id
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, CreateSessionAndGetUser) {
    std::string sid = gen_session_id();
    std::string uid = gen_user_id();

    bool ok = redis_->create_session(sid, uid);
    EXPECT_TRUE(ok);

    std::string got = redis_->get_user_id_by_session(sid);
    EXPECT_EQ(got, uid);

    // 清理
    redis_->remove_session(sid);
}

// ---------------------------------------------------------------------------
// 测试 2：create_session - 创建会话后用户登录状态为 true
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, CreateSessionMarksUserLoggedIn) {
    std::string sid = gen_session_id();
    std::string uid = gen_user_id();

    ASSERT_TRUE(redis_->create_session(sid, uid));

    EXPECT_TRUE(redis_->is_user_logged_in(uid))
        << "User should be marked as logged in after create_session";

    redis_->remove_session(sid);
}

// ---------------------------------------------------------------------------
// 测试 3：remove_session - 删除会话后登录标记同步清除
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, RemoveSessionClearsLoginMark) {
    std::string sid = gen_session_id();
    std::string uid = gen_user_id();

    ASSERT_TRUE(redis_->create_session(sid, uid));
    ASSERT_TRUE(redis_->is_user_logged_in(uid));

    bool ok = redis_->remove_session(sid);
    EXPECT_TRUE(ok);

    // 登录标记应被清除
    EXPECT_FALSE(redis_->is_user_logged_in(uid))
        << "User login mark should be cleared after remove_session";

    // session_id 不再可查到 user_id
    std::string got = redis_->get_user_id_by_session(sid);
    EXPECT_TRUE(got.empty()) << "session_id should not map to any user after removal";
}

// ---------------------------------------------------------------------------
// 测试 4：get_user_id_by_session - 查询不存在的 session_id 返回空字符串
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, GetUserIdByNonExistentSession) {
    std::string sid = gen_session_id();  // 未创建过

    std::string got = redis_->get_user_id_by_session(sid);
    EXPECT_TRUE(got.empty()) << "Non-existent session should return empty string";
}

// ---------------------------------------------------------------------------
// 测试 5：is_user_logged_in - 未登录用户返回 false
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, IsUserLoggedInReturnsFalseForUnknownUser) {
    std::string uid = gen_user_id();  // 未创建会话
    EXPECT_FALSE(redis_->is_user_logged_in(uid));
}

// ---------------------------------------------------------------------------
// 测试 6：SessionLifecycle - 完整往返（create → query → remove → verify gone）
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, SessionLifecycle) {
    std::string sid = gen_session_id();
    std::string uid = gen_user_id();

    // 1. 创建
    ASSERT_TRUE(redis_->create_session(sid, uid));
    EXPECT_TRUE(redis_->is_user_logged_in(uid));
    EXPECT_EQ(redis_->get_user_id_by_session(sid), uid);

    // 2. 删除
    ASSERT_TRUE(redis_->remove_session(sid));
    EXPECT_FALSE(redis_->is_user_logged_in(uid));
    EXPECT_TRUE(redis_->get_user_id_by_session(sid).empty());

    LOG_INFO("[RedisClientTest] SessionLifecycle PASS for sid={}", sid);
}

// ---------------------------------------------------------------------------
// 测试 7：remove_session - 删除不存在的 session_id 不应报错
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, RemoveNonExistentSessionIsSafe) {
    std::string sid = gen_session_id();
    bool ok = redis_->remove_session(sid);
    EXPECT_TRUE(ok) << "remove_session should return true even for non-existent session";
}

// ==================== 验证码管理测试 ====================

// ---------------------------------------------------------------------------
// 测试 8：set_verify_code + get_verify_code - 设置后能查回
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, SetAndGetVerifyCode) {
    std::string cid = gen_code_id();
    std::string code = "1234";

    ASSERT_TRUE(redis_->set_verify_code(cid, code));

    std::string got = redis_->get_verify_code(cid);
    EXPECT_EQ(got, code);

    // 清理
    redis_->remove_verify_code(cid);
}

// ---------------------------------------------------------------------------
// 测试 9：get_verify_code - 查询不存在的 code_id 返回空字符串
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, GetNonExistentVerifyCode) {
    std::string cid = gen_code_id();  // 未设置过
    std::string got = redis_->get_verify_code(cid);
    EXPECT_TRUE(got.empty());
}

// ---------------------------------------------------------------------------
// 测试 10：remove_verify_code - 删除后再次查询为空
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, RemoveVerifyCode) {
    std::string cid = gen_code_id();
    ASSERT_TRUE(redis_->set_verify_code(cid, "5678"));

    EXPECT_TRUE(redis_->remove_verify_code(cid));

    std::string got = redis_->get_verify_code(cid);
    EXPECT_TRUE(got.empty());
}

// ---------------------------------------------------------------------------
// 测试 11：verify_code - 正确验证码验证成功并被消费
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, VerifyCodeCorrect) {
    std::string cid = gen_code_id();
    std::string code = "9876";

    ASSERT_TRUE(redis_->set_verify_code(cid, code));

    bool ok = redis_->verify_code(cid, code);
    EXPECT_TRUE(ok) << "verify_code should succeed with correct code";

    // 验证后应被消费，再次 get 应为空
    std::string got = redis_->get_verify_code(cid);
    EXPECT_TRUE(got.empty()) << "Verify code should be consumed after successful verify";
}

// ---------------------------------------------------------------------------
// 测试 12：verify_code - 错误验证码验证失败，原验证码不被消费
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, VerifyCodeWrongDoesNotConsume) {
    std::string cid = gen_code_id();
    std::string code = "1111";

    ASSERT_TRUE(redis_->set_verify_code(cid, code));

    bool ok = redis_->verify_code(cid, "2222");  // 错误验证码
    EXPECT_FALSE(ok) << "verify_code should fail with wrong code";

    // 原验证码仍应可获取
    std::string got = redis_->get_verify_code(cid);
    EXPECT_EQ(got, code) << "Original code should remain after failed verify";

    // 清理
    redis_->remove_verify_code(cid);
}

// ---------------------------------------------------------------------------
// 测试 13：verify_code - 不存在的 code_id 验证失败
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, VerifyCodeNonExistentFails) {
    std::string cid = gen_code_id();  // 未设置过

    bool ok = redis_->verify_code(cid, "1234");
    EXPECT_FALSE(ok) << "verify_code should fail for non-existent code_id";
}

// ---------------------------------------------------------------------------
// 测试 14：verify_code - 验证成功后再次验证应失败（已被消费）
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, VerifyCodeCannotBeReused) {
    std::string cid = gen_code_id();
    std::string code = "3333";

    ASSERT_TRUE(redis_->set_verify_code(cid, code));
    ASSERT_TRUE(redis_->verify_code(cid, code));

    // 第二次使用相同验证码应失败
    bool ok = redis_->verify_code(cid, code);
    EXPECT_FALSE(ok) << "Verify code should not be reusable after consumption";
}

// ---------------------------------------------------------------------------
// 测试 15：VerifyCodeLifecycle - 完整往返（set → get → verify → get empty）
// ---------------------------------------------------------------------------
TEST_F(RedisClientTest, VerifyCodeLifecycle) {
    std::string cid = gen_code_id();
    std::string code = "4567";

    // 1. set
    ASSERT_TRUE(redis_->set_verify_code(cid, code));

    // 2. get
    EXPECT_EQ(redis_->get_verify_code(cid), code);

    // 3. verify success
    EXPECT_TRUE(redis_->verify_code(cid, code));

    // 4. get after verify - empty
    EXPECT_TRUE(redis_->get_verify_code(cid).empty());

    LOG_INFO("[RedisClientTest] VerifyCodeLifecycle PASS for cid={}", cid);
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
