// =============================================================================
// user_es_test.cc - 用户 ES 数据管理 gtest 测试
// =============================================================================
// 基于 Google Test 框架测试 user/user_es.hpp 的全部功能：
//   1. es_user_to_json          - ESUser → JSON 转换
//   2. create_index              - 创建用户索引（含 ik_max_word 分词器配置）
//   3. insert_data               - 新增用户文档
//   4. update_data               - 更新用户文档（覆盖写入）
//   5. search_by_user_id         - 按关键字搜索（命中 nickname / user_id / email）
//   6. search_by_user_id 排除列表 - 通过 must_not term 过滤指定用户
//   7. delete_data               - 删除用户文档
//   8. FullLifecycle             - 端到端往返测试
//
// 测试策略：
//   - Test Fixture 管理 UserES 实例与唯一 user_id 生成（基于时间戳）
//   - 每个用例使用独立 user_id，避免相互污染
//   - 搜索类测试采用 round-trip：先 insert，再 search 验证命中
//   - 删除测试验证：删除后再次搜索不应命中该 user_id
//
// 运行前提：
//   - Elasticsearch 7.x+ 服务运行在 127.0.0.1:9200
//   - 已安装 ik_max_word 分词器插件
//
// 运行方式：
//   ./user_es_test
//   ./user_es_test --es_host=127.0.0.1 --es_port=9200
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>

#include "user_es.hpp"
#include "log.hpp"

// ==================== gflags 命令行参数定义 ====================

DEFINE_string(es_host, "127.0.0.1", "Elasticsearch server host");
DEFINE_int32(es_port, 9200, "Elasticsearch server port");
// 索引名加 random 后缀避免多次运行相互影响（如需固定可显式指定）
DEFINE_string(es_index, "user_test", "ES index name for user documents");

// ==================== 测试夹具 ====================

/**
 * @brief UserES 测试夹具
 *
 * 管理共享的 UserES 客户端实例，并提供唯一 user_id 生成工具。
 * 在 SetUpTestSuite 中创建索引一次，所有用例共享。
 */
class UserESTest : public ::testing::Test {
protected:
    // 共享的 UserES 客户端（线程安全）
    static std::shared_ptr<user_es::UserES> es_;

    // 自增计数器，用于生成唯一 user_id
    static std::atomic<uint64_t> counter_;

    /**
     * @brief 测试套件前置：初始化日志、构造 UserES、创建索引
     */
    static void SetUpTestSuite() {
        mylog::init(true, "", mylog::LogLevel::INFO);

        es_ = std::make_shared<user_es::UserES>(
            FLAGS_es_host, FLAGS_es_port, FLAGS_es_index);

        // 创建索引（如已存在 ES 会返回 200，不影响测试）
        bool ok = es_->create_index();
        ASSERT_TRUE(ok) << "Failed to create ES index: " << FLAGS_es_index;

        // 等待分片就绪，避免立即写入报错
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    static void TearDownTestSuite() {
        // 不主动删除索引，保留数据便于人工排查
        // 如需清理：es_->client_->delete_index(FLAGS_es_index);
    }

    /**
     * @brief 生成全局唯一 user_id（基于时间戳 + 计数器）
     */
    static std::string gen_user_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "uid_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }

    /**
     * @brief 构造一个测试用 ESUser
     */
    static user_es::ESUser make_user(const std::string& uid,
                                      const std::string& nickname_prefix = "测试用户") {
        user_es::ESUser u;
        u.user_id     = uid;
        u.nickname    = nickname_prefix + "_" + uid;
        u.email       = uid + "@test.com";
        u.description = "desc for " + uid;
        u.avatar_id   = "avatar_" + uid;
        return u;
    }

    /**
     * @brief 等待 ES 索引刷新（使写入立即可搜索）
     */
    static void wait_for_refresh() {
        // ES 默认 1s 刷新，简单等待
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};

// 静态成员定义
std::shared_ptr<user_es::UserES> UserESTest::es_;
std::atomic<uint64_t> UserESTest::counter_{0};

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：es_user_to_json - 字段转换正确性
// ---------------------------------------------------------------------------
TEST_F(UserESTest, EsUserToJsonConversion) {
    user_es::ESUser u;
    u.user_id     = "uid_001";
    u.nickname    = "张三";
    u.email       = "zhangsan@test.com";
    u.description = "Hello";
    u.avatar_id   = "avatar_001";

    Json::Value j = user_es::es_user_to_json(u);

    EXPECT_EQ(j["user_id"].asString(), "uid_001");
    EXPECT_EQ(j["nickname"].asString(), "张三");
    EXPECT_EQ(j["email"].asString(), "zhangsan@test.com");
    EXPECT_EQ(j["description"].asString(), "Hello");
    EXPECT_EQ(j["avatar_id"].asString(), "avatar_001");
}

// ---------------------------------------------------------------------------
// 测试 2：insert_data + search_by_user_id - 按 user_id 精确匹配
// ---------------------------------------------------------------------------
TEST_F(UserESTest, InsertAndSearchByUserId) {
    std::string uid = gen_user_id();
    auto u = make_user(uid);

    ASSERT_TRUE(es_->insert_data(u)) << "Insert failed for " << uid;

    wait_for_refresh();

    std::vector<user_es::ESUser> result;
    bool ok = es_->search_by_user_id(uid, result);
    ASSERT_TRUE(ok);

    // 应至少命中一条
    ASSERT_FALSE(result.empty()) << "No result for user_id=" << uid;

    // 验证命中文档的 user_id 与写入一致
    bool found = false;
    for (const auto& r : result) {
        if (r.user_id == uid) {
            found = true;
            EXPECT_EQ(r.nickname, u.nickname);
            EXPECT_EQ(r.email, u.email);
            break;
        }
    }
    EXPECT_TRUE(found) << "Inserted user_id not found in search result";

    // 清理
    es_->delete_data(uid);
}

// ---------------------------------------------------------------------------
// 测试 3：insert_data + search_by_user_id - 按昵称匹配（中文分词）
// ---------------------------------------------------------------------------
TEST_F(UserESTest, InsertAndSearchByNickname) {
    std::string uid = gen_user_id();
    auto u = make_user(uid, "魔法师小明");

    ASSERT_TRUE(es_->insert_data(u));
    wait_for_refresh();

    std::vector<user_es::ESUser> result;
    // 用昵称子串搜索，依赖 ik_max_word 分词
    ASSERT_TRUE(es_->search_by_user_id("魔法师", result));

    bool found = false;
    for (const auto& r : result) {
        if (r.user_id == uid) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Search by nickname (ik_max_word) failed for " << uid;

    es_->delete_data(uid);
}

// ---------------------------------------------------------------------------
// 测试 4：insert_data + search_by_user_id - 按邮箱精确匹配
// ---------------------------------------------------------------------------
TEST_F(UserESTest, InsertAndSearchByEmail) {
    std::string uid = gen_user_id();
    auto u = make_user(uid);

    ASSERT_TRUE(es_->insert_data(u));
    wait_for_refresh();

    std::vector<user_es::ESUser> result;
    // keyword 字段需完全匹配
    ASSERT_TRUE(es_->search_by_user_id(u.email, result));

    bool found = false;
    for (const auto& r : result) {
        if (r.user_id == uid) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Search by email failed for " << uid;

    es_->delete_data(uid);
}

// ---------------------------------------------------------------------------
// 测试 5：update_data - 覆盖写入后字段变更生效
// ---------------------------------------------------------------------------
TEST_F(UserESTest, UpdateDataOverwritesFields) {
    std::string uid = gen_user_id();
    auto u = make_user(uid, "旧昵称");
    u.description = "old description";

    ASSERT_TRUE(es_->insert_data(u));
    wait_for_refresh();

    // 修改字段后 update
    u.nickname    = "新昵称_" + uid;
    u.description = "new description";
    ASSERT_TRUE(es_->update_data(u));
    wait_for_refresh();

    // 通过 user_id 搜索，验证字段已更新
    std::vector<user_es::ESUser> result;
    ASSERT_TRUE(es_->search_by_user_id(uid, result));

    bool found = false;
    for (const auto& r : result) {
        if (r.user_id == uid) {
            found = true;
            EXPECT_EQ(r.nickname, u.nickname) << "nickname not updated";
            EXPECT_EQ(r.description, "new description") << "description not updated";
            break;
        }
    }
    EXPECT_TRUE(found);

    es_->delete_data(uid);
}

// ---------------------------------------------------------------------------
// 测试 6：search_by_user_id + exclude_uids - 排除指定用户
// ---------------------------------------------------------------------------
TEST_F(UserESTest, SearchWithExclusion) {
    // 插入两个用户，使用相同的昵称前缀以便一起命中
    std::string uid1 = gen_user_id();
    std::string uid2 = gen_user_id();
    auto u1 = make_user(uid1, "共同前缀用户");
    auto u2 = make_user(uid2, "共同前缀用户");

    ASSERT_TRUE(es_->insert_data(u1));
    ASSERT_TRUE(es_->insert_data(u2));
    wait_for_refresh();

    // 不带排除：两条都应命中
    std::vector<user_es::ESUser> result_all;
    ASSERT_TRUE(es_->search_by_user_id("共同前缀", result_all));

    bool hit1 = false, hit2 = false;
    for (const auto& r : result_all) {
        if (r.user_id == uid1) hit1 = true;
        if (r.user_id == uid2) hit2 = true;
    }
    EXPECT_TRUE(hit1) << "uid1 should be in result_all";
    EXPECT_TRUE(hit2) << "uid2 should be in result_all";

    // 排除 uid1：仅 uid2 应命中
    std::vector<user_es::ESUser> result_excluded;
    ASSERT_TRUE(es_->search_by_user_id("共同前缀", result_excluded, {uid1}));

    bool hit1_ex = false, hit2_ex = false;
    for (const auto& r : result_excluded) {
        if (r.user_id == uid1) hit1_ex = true;
        if (r.user_id == uid2) hit2_ex = true;
    }
    EXPECT_FALSE(hit1_ex) << "uid1 should be excluded";
    EXPECT_TRUE(hit2_ex)  << "uid2 should still be present";

    es_->delete_data(uid1);
    es_->delete_data(uid2);
}

// ---------------------------------------------------------------------------
// 测试 7：delete_data - 删除后无法被搜索到
// ---------------------------------------------------------------------------
TEST_F(UserESTest, DeleteDataRemovesDocument) {
    std::string uid = gen_user_id();
    auto u = make_user(uid);

    ASSERT_TRUE(es_->insert_data(u));
    wait_for_refresh();

    // 验证存在
    std::vector<user_es::ESUser> before;
    ASSERT_TRUE(es_->search_by_user_id(uid, before));
    bool exists_before = false;
    for (const auto& r : before) {
        if (r.user_id == uid) exists_before = true;
    }
    EXPECT_TRUE(exists_before);

    // 删除
    ASSERT_TRUE(es_->delete_data(uid));
    wait_for_refresh();

    // 验证不再命中
    std::vector<user_es::ESUser> after;
    ASSERT_TRUE(es_->search_by_user_id(uid, after));
    bool exists_after = false;
    for (const auto& r : after) {
        if (r.user_id == uid) exists_after = true;
    }
    EXPECT_FALSE(exists_after) << "Document should be deleted from ES";
}

// ---------------------------------------------------------------------------
// 测试 8：FullLifecycle - 完整往返（insert → update → search → delete）
// ---------------------------------------------------------------------------
TEST_F(UserESTest, FullLifecycle) {
    std::string uid = gen_user_id();
    auto u = make_user(uid, "生命周期测试");

    // 1. insert
    ASSERT_TRUE(es_->insert_data(u));
    wait_for_refresh();

    // 2. update 修改 description
    u.description = "updated in lifecycle";
    ASSERT_TRUE(es_->update_data(u));
    wait_for_refresh();

    // 3. search 验证 description 已更新
    std::vector<user_es::ESUser> result;
    ASSERT_TRUE(es_->search_by_user_id(uid, result));
    bool verified = false;
    for (const auto& r : result) {
        if (r.user_id == uid) {
            EXPECT_EQ(r.description, "updated in lifecycle");
            verified = true;
            break;
        }
    }
    ASSERT_TRUE(verified);

    // 4. delete 并验证
    ASSERT_TRUE(es_->delete_data(uid));
    wait_for_refresh();

    std::vector<user_es::ESUser> after_delete;
    ASSERT_TRUE(es_->search_by_user_id(uid, after_delete));
    bool still_exists = false;
    for (const auto& r : after_delete) {
        if (r.user_id == uid) still_exists = true;
    }
    EXPECT_FALSE(still_exists);

    LOG_INFO("[UserESTest] FullLifecycle PASS for uid={}", uid);
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
