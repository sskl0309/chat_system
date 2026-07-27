// =============================================================================
// transmit_table.hpp - 消息转发服务数据库操作类
// =============================================================================
// 本文件定义 TransmitTable 类，封装消息转发服务所需的数据库操作，
// 主要用于从数据库获取会话成员信息。
//
// 核心功能：
//   1. 根据会话ID获取所有成员ID列表
//   2. 根据会话ID获取会话信息（类型、名称等）
//   3. 根据用户ID获取其所属的所有会话ID
//
// 依赖说明：
//   - ODB ORM 框架（MySQL 后端）
//   - chat_session.hxx / chat_session_member.hxx（ODB 映射定义）
// =============================================================================

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <odb/session.hxx>

#include "../odb/chat_session-odb.hxx"
#include "../odb/chat_session_member-odb.hxx"
#include "../common/log.hpp"

namespace transmit_table {

/**
 * @brief 消息转发服务数据库操作类
 * 
 * 封装聊天会话相关的数据库 CRUD 操作，为消息转发服务提供数据支撑。
 * 主要用于根据会话ID查询成员列表，以便确定消息的转发目标。
 */
class TransmitTable {
public:
    /**
     * @brief 智能指针类型定义
     */
    using SessionPtr = std::shared_ptr<chat_session>;
    using MemberPtr = std::shared_ptr<chat_session_member>;

    /**
     * @brief 构造函数
     * 
     * @param db ODB 数据库连接实例
     */
    TransmitTable(std::shared_ptr<odb::database> db) : db_(db) {}

    /**
     * @brief 析构函数
     */
    ~TransmitTable() {}

    /**
     * @brief 根据会话ID获取所有成员ID列表
     * 
     * 从 chat_session_member 表中查询指定会话的所有成员，
     * 返回成员用户ID列表，用于确定消息转发目标。
     * 
     * @param session_id 会话唯一标识
     * @return 成员用户ID列表，如果查询失败返回空列表
     */
    std::vector<std::string> get_session_member_ids(const std::string& session_id) {
        std::vector<std::string> member_ids;

        try {
            odb::transaction t(db_->begin());
            odb::session s;

            // 使用 ODB 查询生成器查询指定会话的所有成员
            typedef odb::query<chat_session_member> query;
            auto result = db_->query<chat_session_member>(query::session_id == session_id);

            // 遍历查询结果，提取用户ID
            for (const auto& member : result) {
                member_ids.push_back(member.user_id());
            }

            t.commit();
            LOG_INFO("[TransmitTable] Get {} members for session: {}", member_ids.size(), session_id);
        } catch (const std::exception& e) {
            LOG_ERROR("[TransmitTable] Failed to get session members: {}", e.what());
            member_ids.clear();
        }

        return member_ids;
    }

    /**
     * @brief 根据会话ID获取会话信息
     * 
     * 从 chat_session 表中查询指定会话的详细信息，包括会话名称和会话类型。
     * 
     * @param session_id 会话唯一标识
     * @return 会话对象指针，如果查询失败返回空指针
     */
    SessionPtr get_session_info(const std::string& session_id) {
        try {
            odb::transaction t(db_->begin());
            odb::session s;

            // 使用 ODB 查询生成器查询指定会话
            typedef odb::query<chat_session> query;
            auto result = db_->query<chat_session>(query::session_id == session_id);

            // 返回第一条匹配的会话记录
            if (!result.empty()) {
                SessionPtr session = std::make_shared<chat_session>(*result.begin());
                t.commit();
                LOG_INFO("[TransmitTable] Get session info: {}", session_id);
                return session;
            }

            t.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("[TransmitTable] Failed to get session info: {}", e.what());
        }

        return nullptr;
    }

    /**
     * @brief 根据用户ID获取其所属的所有会话ID
     * 
     * 从 chat_session_member 表中查询指定用户所属的所有会话，
     * 返回会话ID列表。
     * 
     * @param user_id 用户唯一标识
     * @return 会话ID列表，如果查询失败返回空列表
     */
    std::vector<std::string> get_user_session_ids(const std::string& user_id) {
        std::vector<std::string> session_ids;

        try {
            odb::transaction t(db_->begin());
            odb::session s;

            // 使用 ODB 查询生成器查询指定用户所属的所有会话
            typedef odb::query<chat_session_member> query;
            auto result = db_->query<chat_session_member>(query::user_id == user_id);

            // 遍历查询结果，提取会话ID
            for (const auto& member : result) {
                session_ids.push_back(member.session_id());
            }

            t.commit();
            LOG_INFO("[TransmitTable] Get {} sessions for user: {}", session_ids.size(), user_id);
        } catch (const std::exception& e) {
            LOG_ERROR("[TransmitTable] Failed to get user sessions: {}", e.what());
            session_ids.clear();
        }

        return session_ids;
    }

    /**
     * @brief 检查用户是否属于指定会话
     * 
     * 判断指定用户是否是指定会话的成员，用于权限验证。
     * 
     * @param session_id 会话唯一标识
     * @param user_id    用户唯一标识
     * @return 用户是会话成员返回 true，否则返回 false
     */
    bool is_user_in_session(const std::string& session_id, const std::string& user_id) {
        try {
            odb::transaction t(db_->begin());
            odb::session s;

            // 使用 ODB 查询生成器查询用户与会话的关联关系
            typedef odb::query<chat_session_member> query;
            auto result = db_->query<chat_session_member>(
                query::session_id == session_id && query::user_id == user_id);

            bool exists = !result.empty();
            t.commit();
            
            if (exists) {
                LOG_DEBUG("[TransmitTable] User {} is in session {}", user_id, session_id);
            } else {
                LOG_DEBUG("[TransmitTable] User {} is NOT in session {}", user_id, session_id);
            }
            
            return exists;
        } catch (const std::exception& e) {
            LOG_ERROR("[TransmitTable] Failed to check user in session: {}", e.what());
            return false;
        }
    }

private:
    std::shared_ptr<odb::database> db_;  ///< ODB 数据库连接实例
};

} // namespace transmit_table
