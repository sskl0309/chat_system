// =============================================================================
// message_table_test.cc - 消息数据库操作 gtest 测试
// =============================================================================
// 基于 Google Test 框架测试 message/message_table.hpp 的全部功能：
//   1. insert              - 新增消息记录
//   2. select_by_message_id - 根据消息ID查询
//   3. select_by_message_ids - 根据消息ID列表批量查询
//   4. select_by_time_range - 根据时间范围查询
//   5. select_recent        - 查询最近N条消息
//   6. select_recent_before - 查询指定时间之前的N条消息
//   7. select_nonexistent   - 查询不存在的消息返回 nullptr
//
// 测试策略：
//   - Test Fixture 管理 ODB 数据库实例与 MessageTable 客户端
//   - 每个用例使用基于时间戳的唯一 message_id，避免与已有数据冲突
//   - 插入前先清理测试数据，确保测试独立性
//
// 运行前提：
//   - MySQL 服务运行在 127.0.0.1:3306
//   - 数据库 'chat_message' 已创建且 msg_record 表已通过 message.sql 建表
//
// 运行方式：
//   ./message_table_test
//   ./message_table_test --mysql_user=root --mysql_password=xxx --mysql_db=chat_message
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <odb/mysql/database.hxx>
#include <odb/transaction.hxx>
#include <memory>
#include <string>
#include <chrono>
#include <atomic>
#include <vector>
#include <ctime>

#include "message_table.hpp"
#include "log.hpp"

// ==================== gflags 命令行参数定义 ====================

DEFINE_string(mysql_host, "127.0.0.1", "MySQL server host");
DEFINE_int32(mysql_port, 3306, "MySQL server port");
DEFINE_string(mysql_user, "root", "MySQL user name");
DEFINE_string(mysql_password, "123456", "MySQL password");
DEFINE_string(mysql_db, "chat_message", "MySQL database name");

// ==================== 测试夹具 ====================

/**
 * @brief MessageTable 测试夹具
 *
 * 管理 ODB 数据库连接与 MessageTable 实例。
 * 提供唯一 message_id 生成和测试消息构造工具。
 */
class MessageTableTest : public ::testing::Test {
protected:
    // 共享的 MessageTable 客户端
    static std::shared_ptr<message_table::MessageTable> table_;

    // 自增计数器，用于生成唯一 message_id
    static std::atomic<uint64_t> counter_;

    /**
     * @brief 测试套件前置：初始化日志、构造 ODB 数据库实例
     */
    static void SetUpTestSuite() {
        mylog::init(true, "", mylog::LogLevel::INFO);

        // 构造 ODB MySQL 数据库实例
        auto db = std::make_shared<odb::mysql::database>(
            FLAGS_mysql_user,
            FLAGS_mysql_password,
            FLAGS_mysql_db,
            FLAGS_mysql_host,
            FLAGS_mysql_port);

        table_ = std::make_shared<message_table::MessageTable>(db);

        // 验证数据库连接
        try {
            odb::transaction t(db->begin());
            t.commit();
            LOG_INFO("[MessageTableTest] MySQL connected: {}@{}:{}/{}",
                     FLAGS_mysql_user, FLAGS_mysql_host, FLAGS_mysql_port, FLAGS_mysql_db);
        } catch (const std::exception& e) {
            FAIL() << "Failed to connect MySQL: " << e.what()
                   << ", please check --mysql_host / --mysql_user / --mysql_password / --mysql_db";
        }
    }

    static void TearDownTestSuite() {
        // 不主动删除测试数据，便于人工排查
    }

    /**
     * @brief 生成全局唯一 message_id
     *
     * 格式：test_msg_<timestamp>_<counter>
     * 前缀 'test_msg_' 便于人工清理
     */
    static std::string gen_message_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "test_msg_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }

    /**
     * @brief 生成时间字符串（格式：YYYY-MM-DD HH:MM:SS）
     */
    static std::string gen_time_string(int offset_seconds = 0) {
        auto now = std::chrono::system_clock::now();
        auto offset = std::chrono::seconds(offset_seconds);
        auto t = std::chrono::system_clock::to_time_t(now + offset);
        std::tm tm_time;
        localtime_r(&t, &tm_time);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_time);
        return std::string(buf);
    }

    /**
     * @brief 构造一个完整的测试消息对象（文本消息）
     */
    static msg_record make_text_message(const std::string& msg_id,
                                         const std::string& session_id = "test_session",
                                         const std::string& from_user_id = "test_user",
                                         const std::string& content = "Hello, test message!",
                                         int time_offset = 0) {
        msg_record msg(msg_id);
        msg.to_session_id(session_id);
        msg.created_time(gen_time_string(time_offset));
        msg.from_user_id(from_user_id);
        msg.message_type(0);  // STRING 文本消息
        msg.content(content);
        return msg;
    }

    /**
     * @brief 构造一个图片类型的测试消息
     */
    static msg_record make_image_message(const std::string& msg_id,
                                          const std::string& session_id = "test_session",
                                          int time_offset = 0) {
        msg_record msg(msg_id);
        msg.to_session_id(session_id);
        msg.created_time(gen_time_string(time_offset));
        msg.from_user_id("test_user");
        msg.message_type(1);  // IMAGE 图片消息
        msg.file_id("test_file_id_" + msg_id);
        return msg;
    }

    /**
     * @brief 构造一个文件类型的测试消息
     */
    static msg_record make_file_message(const std::string& msg_id,
                                        const std::string& session_id = "test_session",
                                        int time_offset = 0) {
        msg_record msg(msg_id);
        msg.to_session_id(session_id);
        msg.created_time(gen_time_string(time_offset));
        msg.from_user_id("test_user");
        msg.message_type(2);  // FILE 文件消息
        msg.file_id("test_file_id_" + msg_id);
        msg.filename("test_file.txt");
        msg.filesize(1024);
        return msg;
    }
};

// 静态成员定义
std::shared_ptr<message_table::MessageTable> MessageTableTest::table_;
std::atomic<uint64_t> MessageTableTest::counter_{0};

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：insert - 新增文本消息成功
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, InsertTextMessage) {
    std::string msg_id = gen_message_id();
    msg_record msg = make_text_message(msg_id);

    bool ok = table_->insert(msg);
    EXPECT_TRUE(ok) << "Insert text message failed for " << msg_id;

    // 验证自增 id 已生成
    EXPECT_GT(msg.id(), 0u) << "Auto-increment id should be set after insert";
}

// ---------------------------------------------------------------------------
// 测试 2：insert - 新增图片消息成功
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, InsertImageMessage) {
    std::string msg_id = gen_message_id();
    msg_record msg = make_image_message(msg_id);

    bool ok = table_->insert(msg);
    EXPECT_TRUE(ok) << "Insert image message failed for " << msg_id;
}

// ---------------------------------------------------------------------------
// 测试 3：insert - 新增文件消息成功
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, InsertFileMessage) {
    std::string msg_id = gen_message_id();
    msg_record msg = make_file_message(msg_id);

    bool ok = table_->insert(msg);
    EXPECT_TRUE(ok) << "Insert file message failed for " << msg_id;
}

// ---------------------------------------------------------------------------
// 测试 4：insert - 重复插入同一 message_id 应失败（唯一约束）
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, InsertDuplicateMessageIdFails) {
    std::string msg_id = gen_message_id();
    msg_record msg1 = make_text_message(msg_id);

    ASSERT_TRUE(table_->insert(msg1));

    // 用相同 message_id 再次插入
    msg_record msg2 = make_text_message(msg_id, "test_session_2", "test_user_2", "Duplicate message");
    bool ok = table_->insert(msg2);
    EXPECT_FALSE(ok) << "Duplicate message_id insert should fail due to unique constraint";
}

// ---------------------------------------------------------------------------
// 测试 5：select_by_message_id - 通过消息ID查询
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectByMessageId) {
    std::string msg_id = gen_message_id();
    msg_record msg = make_text_message(msg_id, "select_test_session", "user_select", "Test select content");
    ASSERT_TRUE(table_->insert(msg));

    auto ptr = table_->select_by_message_id(msg_id);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->message_id(), msg_id);
    EXPECT_EQ(ptr->to_session_id().get(), "select_test_session");
    EXPECT_EQ(ptr->from_user_id().get(), "user_select");
    EXPECT_EQ(ptr->message_type(), 0);  // STRING
    EXPECT_EQ(ptr->content().get(), "Test select content");
}

// ---------------------------------------------------------------------------
// 测试 6：select_by_message_id - 查询不存在的消息返回 nullptr
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectNonExistentReturnsNull) {
    std::string msg_id = gen_message_id();  // 未插入过

    auto ptr = table_->select_by_message_id(msg_id);
    EXPECT_EQ(ptr, nullptr) << "Should return nullptr for non-existent message";
}

// ---------------------------------------------------------------------------
// 测试 7：select_by_message_ids - 批量查询
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectByMessageIds) {
    std::string msg_id1 = gen_message_id();
    std::string msg_id2 = gen_message_id();

    msg_record msg1 = make_text_message(msg_id1, "batch_session", "user_batch", "Batch message 1");
    msg_record msg2 = make_text_message(msg_id2, "batch_session", "user_batch", "Batch message 2");
    ASSERT_TRUE(table_->insert(msg1));
    ASSERT_TRUE(table_->insert(msg2));

    std::vector<std::string> ids = {msg_id1, msg_id2};
    auto result = table_->select_by_message_ids(ids);

    EXPECT_EQ(result.size(), 2u) << "Should return both messages";

    // 验证两条记录都正确
    bool found1 = false, found2 = false;
    for (const auto& p : result) {
        if (p->message_id() == msg_id1) {
            found1 = true;
            EXPECT_EQ(p->content().get(), "Batch message 1");
        }
        if (p->message_id() == msg_id2) {
            found2 = true;
            EXPECT_EQ(p->content().get(), "Batch message 2");
        }
    }
    EXPECT_TRUE(found1) << "msg_id1 missing in result";
    EXPECT_TRUE(found2) << "msg_id2 missing in result";
}

// ---------------------------------------------------------------------------
// 测试 8：select_by_message_ids - 空列表入参应返回空结果
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectByMessageIdsEmpty) {
    std::vector<std::string> empty_ids;
    auto result = table_->select_by_message_ids(empty_ids);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// 测试 9：select_by_message_ids - 部分不存在的 ID 只返回存在的
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectByMessageIdsPartialMissing) {
    std::string msg_id = gen_message_id();
    msg_record msg = make_text_message(msg_id, "partial_session");
    ASSERT_TRUE(table_->insert(msg));

    std::vector<std::string> ids = {msg_id, "non_existent_msg_id_" + gen_message_id()};
    auto result = table_->select_by_message_ids(ids);

    EXPECT_EQ(result.size(), 1u) << "Only existing message should be returned";
    EXPECT_EQ(result[0]->message_id(), msg_id);
}

// ---------------------------------------------------------------------------
// 测试 10：select_by_time_range - 根据时间范围查询
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectByTimeRange) {
    std::string session_id = "timerange_test_session";

    // 插入 3 条消息，时间分别为 -60s, -30s, 0s
    std::string msg_id1 = gen_message_id();
    std::string msg_id2 = gen_message_id();
    std::string msg_id3 = gen_message_id();

    msg_record msg1 = make_text_message(msg_id1, session_id, "user_tr", "Message -60s", -60);
    msg_record msg2 = make_text_message(msg_id2, session_id, "user_tr", "Message -30s", -30);
    msg_record msg3 = make_text_message(msg_id3, session_id, "user_tr", "Message 0s", 0);
    ASSERT_TRUE(table_->insert(msg1));
    ASSERT_TRUE(table_->insert(msg2));
    ASSERT_TRUE(table_->insert(msg3));

    // 查询时间范围：-45s 到 -15s（应该只有 msg_id2）
    std::string start_time = gen_time_string(-45);
    std::string end_time = gen_time_string(-15);

    auto result = table_->select_by_time_range(session_id, start_time, end_time);

    EXPECT_EQ(result.size(), 1u) << "Should return 1 message in time range";
    if (!result.empty()) {
        EXPECT_EQ(result[0]->message_id(), msg_id2);
        EXPECT_EQ(result[0]->content().get(), "Message -30s");
    }
}

// ---------------------------------------------------------------------------
// 测试 11：select_by_time_range - 大范围查询包含全部消息
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectByTimeRangeAll) {
    std::string session_id = "timerange_all_session";

    std::string msg_id1 = gen_message_id();
    std::string msg_id2 = gen_message_id();

    msg_record msg1 = make_text_message(msg_id1, session_id, "user_tr", "Message 1", -120);
    msg_record msg2 = make_text_message(msg_id2, session_id, "user_tr", "Message 2", -60);
    ASSERT_TRUE(table_->insert(msg1));
    ASSERT_TRUE(table_->insert(msg2));

    // 查询大范围：-200s 到 10s
    std::string start_time = gen_time_string(-200);
    std::string end_time = gen_time_string(10);

    auto result = table_->select_by_time_range(session_id, start_time, end_time);

    EXPECT_EQ(result.size(), 2u) << "Should return both messages";

    // 验证按时间升序排列（早的在前）
    if (result.size() >= 2) {
        EXPECT_LE(result[0]->created_time(), result[1]->created_time())
            << "Messages should be in ascending time order";
    }
}

// ---------------------------------------------------------------------------
// 测试 12：select_recent - 查询最近N条消息
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectRecent) {
    std::string session_id = "recent_test_session";

    // 插入 5 条消息，时间分别为 -500s, -400s, -300s, -200s, -100s
    std::vector<std::string> msg_ids;
    for (int i = 0; i < 5; i++) {
        std::string mid = gen_message_id();
        msg_ids.push_back(mid);
        msg_record msg = make_text_message(mid, session_id, "user_recent",
            "Recent message " + std::to_string(i), -500 + i * 100);
        ASSERT_TRUE(table_->insert(msg));
    }

    // 查询最近 3 条
    auto result = table_->select_recent(session_id, 3);

    EXPECT_EQ(result.size(), 3u) << "Should return 3 recent messages";

    // 验证是最新的 3 条（msg_ids[2], msg_ids[3], msg_ids[4]）
    // 且按时间升序排列
    if (result.size() >= 2) {
        EXPECT_LE(result[0]->created_time(), result[1]->created_time())
            << "Messages should be in ascending time order";
        EXPECT_LE(result[1]->created_time(), result[2]->created_time())
            << "Messages should be in ascending time order";
    }
}

// ---------------------------------------------------------------------------
// 测试 13：select_recent - 查询数量超过实际消息数
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectRecentMoreThanExist) {
    std::string session_id = "recent_few_session";

    // 只插入 2 条消息
    std::string msg_id1 = gen_message_id();
    std::string msg_id2 = gen_message_id();
    msg_record msg1 = make_text_message(msg_id1, session_id, "user_rf", "Message 1", -200);
    msg_record msg2 = make_text_message(msg_id2, session_id, "user_rf", "Message 2", -100);
    ASSERT_TRUE(table_->insert(msg1));
    ASSERT_TRUE(table_->insert(msg2));

    // 请求 10 条，但只有 2 条
    auto result = table_->select_recent(session_id, 10);

    EXPECT_EQ(result.size(), 2u) << "Should return only 2 messages available";
}

// ---------------------------------------------------------------------------
// 测试 14：select_recent_before - 查询指定时间之前的N条消息
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectRecentBefore) {
    std::string session_id = "before_test_session";

    // 插入 5 条消息
    std::vector<std::string> msg_ids;
    for (int i = 0; i < 5; i++) {
        std::string mid = gen_message_id();
        msg_ids.push_back(mid);
        msg_record msg = make_text_message(mid, session_id, "user_before",
            "Before message " + std::to_string(i), -500 + i * 100);
        ASSERT_TRUE(table_->insert(msg));
    }

    // 截止时间：-250s（只取之前的消息，即 -500s, -400s, -300s）
    std::string before_time = gen_time_string(-250);

    auto result = table_->select_recent_before(session_id, 2, before_time);

    EXPECT_EQ(result.size(), 2u) << "Should return 2 messages before the time";

    // 验证这些消息的时间都在 before_time 之前
    for (const auto& msg : result) {
        EXPECT_LE(msg->created_time(), before_time)
            << "All returned messages should be before the cutoff time";
    }
}

// ---------------------------------------------------------------------------
// 测试 15：select_recent - 不同会话的消息隔离
// ---------------------------------------------------------------------------
TEST_F(MessageTableTest, SelectRecentIsolation) {
    std::string session_a = "isolation_session_a";
    std::string session_b = "isolation_session_b";

    // 会话 A 插入 3 条
    for (int i = 0; i < 3; i++) {
        msg_record msg = make_text_message(gen_message_id(), session_a, "user_iso",
            "Session A message " + std::to_string(i), -300 + i * 100);
        ASSERT_TRUE(table_->insert(msg));
    }

    // 会话 B 插入 2 条
    for (int i = 0; i < 2; i++) {
        msg_record msg = make_text_message(gen_message_id(), session_b, "user_iso",
            "Session B message " + std::to_string(i), -200 + i * 100);
        ASSERT_TRUE(table_->insert(msg));
    }

    // 查询会话 A 的最近 10 条
    auto result_a = table_->select_recent(session_a, 10);
    EXPECT_EQ(result_a.size(), 3u) << "Session A should have 3 messages";

    // 查询会话 B 的最近 10 条
    auto result_b = table_->select_recent(session_b, 10);
    EXPECT_EQ(result_b.size(), 2u) << "Session B should have 2 messages";
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}