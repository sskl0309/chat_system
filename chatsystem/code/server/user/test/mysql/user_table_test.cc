// =============================================================================
// user_table_test.cc - 用户数据库操作 gtest 测试
// =============================================================================
// 基于 Google Test 框架测试 common/user_table.hpp 的全部功能：
//   1. insert                - 新增用户
//   2. select_by_user_id      - 通过用户ID查询
//   3. select_by_nickname     - 通过昵称查询
//   4. select_by_email        - 通过邮箱查询
//   5. select_multi           - 批量查询
//   6. update                 - 更新用户信息
//   7. select_nonexistent     - 查询不存在的用户返回 nullptr
//
// 测试策略：
//   - Test Fixture 管理 ODB 数据库实例与 UserTable 客户端
//   - 每个用例使用基于时间戳的唯一 user_id，避免与已有数据冲突
//   - 由于 UserTable 未提供 delete 接口，测试数据保留在数据库中
//     （人工清理时按 user_id 前缀 'test_' 过滤即可）
//   - update 测试通过修改 description（非唯一字段）验证更新生效
//
// 运行前提：
//   - MySQL 服务运行在 127.0.0.1:3306
//   - 数据库 'chat_user' 已创建且 user 表已通过 user.sql 建表
//
// 运行方式：
//   ./user_table_test
//   ./user_table_test --mysql_user=root --mysql_password=xxx --mysql_db=chat_user
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

#include "user_table.hpp"
#include "log.hpp"

// ==================== gflags 命令行参数定义 ====================

DEFINE_string(mysql_host, "127.0.0.1", "MySQL server host");
DEFINE_int32(mysql_port, 3306, "MySQL server port");
DEFINE_string(mysql_user, "root", "MySQL user name");
DEFINE_string(mysql_password, "", "MySQL password");
DEFINE_string(mysql_db, "chat_user", "MySQL database name");

// ==================== 测试夹具 ====================

/**
 * @brief UserTable 测试夹具
 *
 * 管理 ODB 数据库连接与 UserTable 实例。
 * 提供唯一 user_id 生成和测试用户构造工具。
 */
class UserTableTest : public ::testing::Test {
protected:
    // 共享的 UserTable 客户端
    static std::shared_ptr<user_table::UserTable> table_;

    // 自增计数器，用于生成唯一 user_id
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

        table_ = std::make_shared<user_table::UserTable>(db);

        // 验证数据库连接
        try {
            odb::transaction t(db->begin());
            t.commit();
            LOG_INFO("[UserTableTest] MySQL connected: {}@{}:{}/{}",
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
     * @brief 生成全局唯一 user_id
     *
     * 格式：test_<timestamp>_<counter>
     * 前缀 'test_' 便于人工清理
     */
    static std::string gen_user_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint64_t cnt = counter_.fetch_add(1);
        return "test_" + std::to_string(ms) + "_" + std::to_string(cnt);
    }

    /**
     * @brief 构造一个完整的测试用户对象
     */
    static user make_user(const std::string& uid) {
        // 使用 user(user_id, nickname, passwd) 构造函数
        user u(uid, "nick_" + uid, "passwd_" + uid);
        // 设置可选字段
        u.email(uid + "@test.com");
        u.avatar_id("avatar_" + uid);
        u.description("desc for " + uid);
        return u;
    }
};

// 静态成员定义
std::shared_ptr<user_table::UserTable> UserTableTest::table_;
std::atomic<uint64_t> UserTableTest::counter_{0};

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：insert - 新增用户成功
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, InsertUser) {
    std::string uid = gen_user_id();
    user u = make_user(uid);

    bool ok = table_->insert(u);
    EXPECT_TRUE(ok) << "Insert failed for " << uid;

    // 验证自增 id 已生成
    EXPECT_GT(u.id(), 0u) << "Auto-increment id should be set after insert";
}

// ---------------------------------------------------------------------------
// 测试 2：insert - 重复插入同一 user_id 应失败（唯一约束）
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, InsertDuplicateUserIdFails) {
    std::string uid = gen_user_id();
    user u1 = make_user(uid);

    ASSERT_TRUE(table_->insert(u1));

    // 用相同 user_id 再次插入
    user u2 = make_user(uid);
    u2.nickname("another_nick_" + uid);  // 改个昵称避免 nickname 唯一冲突
    bool ok = table_->insert(u2);
    EXPECT_FALSE(ok) << "Duplicate user_id insert should fail due to unique constraint";
}

// ---------------------------------------------------------------------------
// 测试 3：select_by_user_id - 通过 user_id 查询
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectByUserId) {
    std::string uid = gen_user_id();
    user u = make_user(uid);
    ASSERT_TRUE(table_->insert(u));

    auto ptr = table_->select_by_user_id(uid);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->user_id(), uid);
    EXPECT_EQ(ptr->nickname().get(), "nick_" + uid);
    EXPECT_EQ(ptr->email().get(), uid + "@test.com");
    EXPECT_EQ(ptr->passwd().get(), "passwd_" + uid);
    EXPECT_EQ(ptr->avatar_id().get(), "avatar_" + uid);
    EXPECT_EQ(ptr->description().get(), "desc for " + uid);
}

// ---------------------------------------------------------------------------
// 测试 4：select_by_nickname - 通过昵称查询
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectByNickname) {
    std::string uid = gen_user_id();
    user u = make_user(uid);
    ASSERT_TRUE(table_->insert(u));

    auto ptr = table_->select_by_nickname("nick_" + uid);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->user_id(), uid);
}

// ---------------------------------------------------------------------------
// 测试 5：select_by_email - 通过邮箱查询
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectByEmail) {
    std::string uid = gen_user_id();
    user u = make_user(uid);
    ASSERT_TRUE(table_->insert(u));

    auto ptr = table_->select_by_email(uid + "@test.com");
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->user_id(), uid);
}

// ---------------------------------------------------------------------------
// 测试 6：select_multi - 批量查询
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectMulti) {
    std::string uid1 = gen_user_id();
    std::string uid2 = gen_user_id();

    user u1 = make_user(uid1);
    user u2 = make_user(uid2);
    ASSERT_TRUE(table_->insert(u1));
    ASSERT_TRUE(table_->insert(u2));

    std::vector<std::string> ids = {uid1, uid2};
    auto result = table_->select_multi(ids);

    EXPECT_EQ(result.size(), 2u)
        << "select_multi should return both users";

    // 验证两条记录都正确
    bool found1 = false, found2 = false;
    for (const auto& p : result) {
        if (p.first == uid1 && p.second && p.second->user_id() == uid1) found1 = true;
        if (p.first == uid2 && p.second && p.second->user_id() == uid2) found2 = true;
    }
    EXPECT_TRUE(found1) << "uid1 missing in select_multi result";
    EXPECT_TRUE(found2) << "uid2 missing in select_multi result";
}

// ---------------------------------------------------------------------------
// 测试 7：update - 更新用户信息（修改 description / avatar_id）
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, UpdateUserFields) {
    std::string uid = gen_user_id();
    user u = make_user(uid);
    ASSERT_TRUE(table_->insert(u));

    // 取出后修改非唯一字段
    auto ptr = table_->select_by_user_id(uid);
    ASSERT_NE(ptr, nullptr);

    ptr->description("updated description");
    ptr->avatar_id("updated_avatar");

    bool ok = table_->update(*ptr);
    EXPECT_TRUE(ok);

    // 再次查询验证更新生效
    auto updated = table_->select_by_user_id(uid);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->description().get(), "updated description");
    EXPECT_EQ(updated->avatar_id().get(), "updated_avatar");
    // 未修改的字段保持原值
    EXPECT_EQ(updated->nickname().get(), "nick_" + uid);
}

// ---------------------------------------------------------------------------
// 测试 8：select_by_user_id - 查询不存在的用户返回 nullptr
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectNonExistentReturnsNull) {
    std::string uid = gen_user_id();  // 未插入过

    auto ptr = table_->select_by_user_id(uid);
    EXPECT_EQ(ptr, nullptr) << "Should return nullptr for non-existent user";

    auto ptr_n = table_->select_by_nickname("non_existent_nick_" + uid);
    EXPECT_EQ(ptr_n, nullptr);

    auto ptr_e = table_->select_by_email("non_existent_" + uid + "@test.com");
    EXPECT_EQ(ptr_e, nullptr);
}

// ---------------------------------------------------------------------------
// 测试 9：select_multi - 空列表入参应返回空结果
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectMultiEmpty) {
    std::vector<std::string> empty_ids;
    auto result = table_->select_multi(empty_ids);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// 测试 10：select_multi - 部分不存在的 user_id 应只返回存在的
// ---------------------------------------------------------------------------
TEST_F(UserTableTest, SelectMultiPartialMissing) {
    std::string uid = gen_user_id();
    user u = make_user(uid);
    ASSERT_TRUE(table_->insert(u));

    std::vector<std::string> ids = {uid, "non_existent_uid_" + uid};
    auto result = table_->select_multi(ids);

    EXPECT_EQ(result.size(), 1u) << "Only existing user should be returned";
    EXPECT_EQ(result[0].first, uid);
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
