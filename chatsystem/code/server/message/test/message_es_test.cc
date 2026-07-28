// =============================================================================
// message_es_test.cc - 消息 ES 数据管理 gtest 测试
// =============================================================================
// 基于 Google Test 框架测试 message/message_es.hpp 的全部功能：
//   1. es_message_to_json    - ESMessage → JSON 转换
//   2. create_index            - 创建消息索引（含 ik_max_word 分词器配置）
//   3. insert_message          - 新增文本消息到 ES
//   4. batch_insert            - 批量新增消息
//   5. search_by_keyword       - 根据会话ID和关键字搜索消息
//   6. delete_message          - 删除消息
//   7. FullLifecycle           - 端到端往返测试
//
// 测试策略：
//   - Test Fixture 管理 MessageES 实例与唯一 message_id 生成
//   - 每个用例使用独立 message_id，避免相互污染
//   - 搜索类测试采用 round-trip：先 insert，再 search 验证命中
//
// 运行前提：
//   - Elasticsearch 7.x+ 服务运行在 127.0.0.1:9200
//   - 已安装 ik_max_word 分词器插件
//
// 运行方式：
//   ./message_es_test
//   ./message_es_test --es_host=127.0.0.1 --es_port=9200
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

#include "message_es.hpp"
#include "log.hpp"

// ==================== gflags 命令行参数定义 ====================

DEFINE_string(es_host, "127.0.0.1", "Elasticsearch server host");
DEFINE_int32(es_port, 9200, "Elasticsearch server port");

// ==================== 测试夹具 ====================

/**
 * @brief MessageES 测试夹具
 *
 * 管理共享的 MessageES 客户端实例，并提供唯一 message_id 生成工具。
 */
class MessageESTest : public ::testing::Test {
protected:
    // 共享的 MessageES 客户端
    static std::shared_ptr<message_es::MessageES> es_;

    // 自增计数器，用于生成唯一 message_id
    static std::atomic<uint64_t> counter_;

    /**
     * @brief 测试套件前置：初始化日志、构造 MessageES、创建索引
     */
    static void SetUpTestSuite() {
        mylog::init(true, "", mylog::LogLevel::INFO);

        es_ = std::make_shared<message_es::MessageES>(
            FLAGS_es_host, FLAGS_es_port);

        // 创建索引（如已存在 ES 会返回 200，不影响测试）
        bool ok = es_->create_index();
        ASSERT_TRUE(ok) << "Failed to create ES index";

        // 等待分片就绪
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    static void TearDownTestSuite() {
        // 不主动删除索引，保留数据便于人工排查
    }

    /**
     * @brief 生成全局唯一 message_id
     */
    static std::string gen_message_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "test_msg_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }

    /**
     * @brief 构造一个测试用 ESMessage
     */
    static message_es::ESMessage make_message(const std::string& msg_id,
                                                const std::string& session_id = "test_session",
                                                const std::string& content = "这是一条测试消息") {
        message_es::ESMessage msg;
        msg.chat_session_id = session_id;
        msg.message_id = msg_id;
        msg.content = content;
        return msg;
    }

    /**
     * @brief 等待 ES 索引刷新
     */
    static void wait_for_refresh() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};

// 静态成员定义
std::shared_ptr<message_es::MessageES> MessageESTest::es_;
std::atomic<uint64_t> MessageESTest::counter_{0};

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：es_message_to_json - 字段转换正确性
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, EsMessageToJsonConversion) {
    message_es::ESMessage msg;
    msg.chat_session_id = "session_001";
    msg.message_id = "MSG_001";
    msg.content = "Hello World";

    Json::Value j = message_es::es_message_to_json(msg);

    EXPECT_EQ(j["chat_session_id"].asString(), "session_001");
    EXPECT_EQ(j["message_id"].asString(), "MSG_001");
    EXPECT_EQ(j["content"].asString(), "Hello World");
}

// ---------------------------------------------------------------------------
// 测试 2：insert_message + search_by_keyword - 按会话ID和关键字搜索
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, InsertAndSearchByKeyword) {
    std::string msg_id = gen_message_id();
    auto msg = make_message(msg_id, "search_session", "这是一条包含关键字的测试消息");

    ASSERT_TRUE(es_->insert_message(msg)) << "Insert failed for " << msg_id;

    wait_for_refresh();

    std::vector<message_es::ESMessage> result;
    bool ok = es_->search_by_keyword("search_session", "关键字", result);
    ASSERT_TRUE(ok);

    // 应至少命中一条
    ASSERT_FALSE(result.empty()) << "No result for search";

    // 验证命中文档
    bool found = false;
    for (const auto& r : result) {
        if (r.message_id == msg_id) {
            found = true;
            EXPECT_EQ(r.content, msg.content);
            EXPECT_EQ(r.chat_session_id, "search_session");
            break;
        }
    }
    EXPECT_TRUE(found) << "Inserted message not found in search result";
}

// ---------------------------------------------------------------------------
// 测试 3：insert_message + search_by_keyword - 中文分词搜索
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, InsertAndSearchByChineseKeyword) {
    std::string msg_id = gen_message_id();
    auto msg = make_message(msg_id, "chinese_session", "今天天气真好，适合出去散步");

    ASSERT_TRUE(es_->insert_message(msg));
    wait_for_refresh();

    std::vector<message_es::ESMessage> result;
    // 用中文子串搜索，依赖 ik_max_word 分词
    ASSERT_TRUE(es_->search_by_keyword("chinese_session", "天气", result));

    bool found = false;
    for (const auto& r : result) {
        if (r.message_id == msg_id) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Search by Chinese keyword failed";
}

// ---------------------------------------------------------------------------
// 测试 4：search_by_keyword - 不同会话隔离
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, SearchSessionIsolation) {
    // 在两个不同会话中插入相同内容的消息
    std::string msg_id1 = gen_message_id();
    std::string msg_id2 = gen_message_id();

    es_->insert_message(make_message(msg_id1, "session_A", "隔离测试消息"));
    es_->insert_message(make_message(msg_id2, "session_B", "隔离测试消息"));
    wait_for_refresh();

    // 搜索 session_A，应该只找到 msg_id1
    std::vector<message_es::ESMessage> result_a;
    ASSERT_TRUE(es_->search_by_keyword("session_A", "隔离", result_a));

    bool found1 = false, found2 = false;
    for (const auto& r : result_a) {
        if (r.message_id == msg_id1) found1 = true;
        if (r.message_id == msg_id2) found2 = true;
    }
    EXPECT_TRUE(found1) << "session_A should contain msg_id1";
    EXPECT_FALSE(found2) << "session_A should NOT contain msg_id2";
}

// ---------------------------------------------------------------------------
// 测试 5：batch_insert - 批量插入消息
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, BatchInsertMessages) {
    std::vector<message_es::ESMessage> messages;
    std::vector<std::string> msg_ids;

    for (int i = 0; i < 5; i++) {
        std::string mid = gen_message_id();
        msg_ids.push_back(mid);
        messages.push_back(make_message(mid, "batch_session", "批量测试消息 " + std::to_string(i)));
    }

    ASSERT_TRUE(es_->batch_insert(messages));
    wait_for_refresh();

    // 搜索验证所有消息都能被找到
    for (const auto& mid : msg_ids) {
        std::vector<message_es::ESMessage> result;
        // 使用 message_id 作为关键字搜索（message_id 是 keyword 类型）
        // 这里用 content 中的文字搜索
        std::string expected_content = "批量测试消息";
        ASSERT_TRUE(es_->search_by_keyword("batch_session", expected_content, result));

        bool found = false;
        for (const auto& r : result) {
            if (r.message_id == mid) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Batch inserted message " << mid << " not found";
    }
}

// ---------------------------------------------------------------------------
// 测试 6：delete_message - 删除后无法被搜索到
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, DeleteMessageRemovesDocument) {
    std::string msg_id = gen_message_id();
    auto msg = make_message(msg_id, "delete_session", "待删除的消息");

    ASSERT_TRUE(es_->insert_message(msg));
    wait_for_refresh();

    // 验证存在
    std::vector<message_es::ESMessage> before;
    ASSERT_TRUE(es_->search_by_keyword("delete_session", "待删除", before));
    bool exists_before = false;
    for (const auto& r : before) {
        if (r.message_id == msg_id) exists_before = true;
    }
    EXPECT_TRUE(exists_before);

    // 删除
    ASSERT_TRUE(es_->delete_message(msg_id));
    wait_for_refresh();

    // 验证不再命中
    std::vector<message_es::ESMessage> after;
    ASSERT_TRUE(es_->search_by_keyword("delete_session", "待删除", after));
    bool exists_after = false;
    for (const auto& r : after) {
        if (r.message_id == msg_id) exists_after = true;
    }
    EXPECT_FALSE(exists_after) << "Message should be deleted from ES";
}

// ---------------------------------------------------------------------------
// 测试 7：search_by_keyword - 搜索不存在的关键字返回空
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, SearchNonExistentKeyword) {
    std::vector<message_es::ESMessage> result;
    bool ok = es_->search_by_keyword("nonexistent_session", "不存在的关键字", result);
    ASSERT_TRUE(ok);  // 搜索本身应该成功（即使没有结果）
    EXPECT_TRUE(result.empty()) << "Should return empty for non-existent keyword";
}

// ---------------------------------------------------------------------------
// 测试 8：FullLifecycle - 完整往返（insert → search → delete）
// ---------------------------------------------------------------------------
TEST_F(MessageESTest, FullLifecycle) {
    std::string msg_id = gen_message_id();
    auto msg = make_message(msg_id, "lifecycle_session", "生命周期测试消息");

    // 1. insert
    ASSERT_TRUE(es_->insert_message(msg));
    wait_for_refresh();

    // 2. search 验证
    std::vector<message_es::ESMessage> result;
    ASSERT_TRUE(es_->search_by_keyword("lifecycle_session", "生命周期", result));
    bool verified = false;
    for (const auto& r : result) {
        if (r.message_id == msg_id) {
            EXPECT_EQ(r.content, "生命周期测试消息");
            verified = true;
            break;
        }
    }
    ASSERT_TRUE(verified);

    // 3. delete 并验证
    ASSERT_TRUE(es_->delete_message(msg_id));
    wait_for_refresh();

    std::vector<message_es::ESMessage> after_delete;
    ASSERT_TRUE(es_->search_by_keyword("lifecycle_session", "生命周期", after_delete));
    bool still_exists = false;
    for (const auto& r : after_delete) {
        if (r.message_id == msg_id) still_exists = true;
    }
    EXPECT_FALSE(still_exists);

    LOG_INFO("[MessageESTest] FullLifecycle PASS for msg_id={}", msg_id);
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}