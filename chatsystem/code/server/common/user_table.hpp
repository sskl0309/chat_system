// =============================================================================
// user_table.hpp - 用户数据数据库操作模块
// =============================================================================
// 基于 odb-mysql 封装的用户数据管理模块，实现关系型数据库中用户数据的增删改查。
//
// 提供的操作：
//   1. 通过昵称获取用户信息
//   2. 通过邮箱获取用户信息
//   3. 通过用户ID获取用户信息
//   4. 新增用户
//   5. 更新用户信息
//
// 依赖说明：
//   - odb: ODB ORM 框架
//   - odb-mysql: MySQL 数据库后端
//   - user.hxx / user-odb.hxx: ODB 持久化类及生成代码
// =============================================================================

#ifndef USER_TABLE_HPP
#define USER_TABLE_HPP

#include <memory>
#include <string>
#include <odb/database.hxx>
#include <odb/mysql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/result.hxx>
#include <odb/query.hxx>
#include <odb/nullable.hxx>

#include "user.hxx"
#include "user-odb.hxx"
#include "log.hpp"

namespace user_table {

/**
 * @brief 用户数据管理类
 *
 * 封装 ODB 数据库操作，提供用户信息的增删改查接口。
 * 通过共享的 odb::database 实例进行数据库访问。
 */
class UserTable {
public:
    using UserPtr = std::shared_ptr<user>;

    /**
     * @brief 构造函数
     * @param db ODB 数据库实例智能指针
     */
    UserTable(std::shared_ptr<odb::database> db) : db_(db) {}

    /**
     * @brief 通过昵称获取用户信息
     * @param nickname 用户昵称
     * @return 用户信息智能指针，未找到返回 nullptr
     */
    UserPtr select_by_nickname(const std::string& nickname) {
        try {
            typedef odb::query<user> query;
            typedef odb::result<user> result;

            odb::transaction t(db_->begin());
            result r(db_->query<user>(query::nickname == nickname));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            // 将结果转移到堆上，避免迭代器失效
            UserPtr u = std::make_shared<user>(*r.begin());
            t.commit();
            return u;
        } catch (const std::exception& e) {
            LOG_ERROR("[UserTable] select_by_nickname failed: {}", e.what());
            return nullptr;
        }
    }

    /**
     * @brief 通过邮箱获取用户信息
     * @param email 用户邮箱
     * @return 用户信息智能指针，未找到返回 nullptr
     */
    UserPtr select_by_email(const std::string& email) {
        try {
            typedef odb::query<user> query;
            typedef odb::result<user> result;

            odb::transaction t(db_->begin());
            result r(db_->query<user>(query::email == email));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            UserPtr u = std::make_shared<user>(*r.begin());
            t.commit();
            return u;
        } catch (const std::exception& e) {
            LOG_ERROR("[UserTable] select_by_email failed: {}", e.what());
            return nullptr;
        }
    }

    /**
     * @brief 通过用户ID获取用户信息
     * @param user_id 用户唯一标识
     * @return 用户信息智能指针，未找到返回 nullptr
     */
    UserPtr select_by_user_id(const std::string& user_id) {
        try {
            typedef odb::query<user> query;
            typedef odb::result<user> result;

            odb::transaction t(db_->begin());
            result r(db_->query<user>(query::user_id == user_id));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            UserPtr u = std::make_shared<user>(*r.begin());
            t.commit();
            return u;
        } catch (const std::exception& e) {
            LOG_ERROR("[UserTable] select_by_user_id failed: {}", e.what());
            return nullptr;
        }
    }

    /**
     * @brief 批量获取用户信息
     * @param user_ids 用户ID列表
     * @return 用户信息列表，每个元素为 {user_id, UserPtr}
     */
    std::vector<std::pair<std::string, UserPtr>> select_multi(const std::vector<std::string>& user_ids) {
        std::vector<std::pair<std::string, UserPtr>> result;
        if (user_ids.empty()) {
            return result;
        }
        try {
            odb::transaction t(db_->begin());
            for (const auto& uid : user_ids) {
                typedef odb::query<user> query;
                typedef odb::result<user> res_type;
                res_type r(db_->query<user>(query::user_id == uid));
                auto it = r.begin();
                if (it != r.end()) {
                    UserPtr u = std::make_shared<user>(*it);
                    result.emplace_back(uid, u);
                }
            }
            t.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("[UserTable] select_multi failed: {}", e.what());
        }
        return result;
    }

    /**
     * @brief 新增用户
     * @param u 用户信息（需包含 user_id，可选包含 nickname、passwd 等）
     * @return 成功返回 true，失败返回 false
     */
    bool insert(user& u) {
        try {
            odb::transaction t(db_->begin());
            db_->persist(u);
            t.commit();
            LOG_INFO("[UserTable] Insert user success, user_id: {}", u.user_id());
            return true;
        } catch (const odb::object_already_persistent& e) {
            LOG_WARN("[UserTable] User already exists: {}", e.what());
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("[UserTable] Insert failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 更新用户信息
     * @param u 用户信息（需包含完整的用户数据）
     * @return 成功返回 true，失败返回 false
     */
    bool update(const user& u) {
        try {
            odb::transaction t(db_->begin());
            db_->update(u);
            t.commit();
            LOG_INFO("[UserTable] Update user success, user_id: {}", u.user_id());
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[UserTable] Update failed: {}", e.what());
            return false;
        }
    }

private:
    std::shared_ptr<odb::database> db_;  ///< ODB 数据库实例
};

} // namespace user_table

#endif // USER_TABLE_HPP
