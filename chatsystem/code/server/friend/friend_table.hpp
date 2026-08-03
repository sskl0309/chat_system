// =============================================================================
// friend_table.hpp - 好友管理数据数据库操作模块
// =============================================================================
// 本文件封装好友管理子服务的所有数据库操作，使用 ODB ORM 框架访问 MySQL。
//
// 功能职责：
//   1. 好友关系管理：查询/添加/删除好友关系
//   2. 好友申请事件管理：创建/查询/删除好友申请
//   3. 聊天会话管理：创建/删除会话，管理会话成员
//
// 涉及数据表：
//   - friend_relation       : 好友关系表（双向存储）
//   - friend_event          : 好友申请事件表
//   - chat_session          : 聊天会话表
//   - chat_session_member   : 会话成员关联表
// =============================================================================
#ifndef FRIEND_TABLE_HPP
#define FRIEND_TABLE_HPP

#include <memory>
#include <string>
#include <vector>

#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <odb/query.hxx>
#include <odb/result.hxx>

#include "friend_relation-odb.hxx"
#include "friend_event-odb.hxx"
#include "chat_session-odb.hxx"
#include "chat_session_member-odb.hxx"
#include "../common/log.hpp"

namespace friend_table {

/**
 * @brief 好友管理数据库操作类
 *
 * 封装所有与好友管理相关的 MySQL 数据库操作，包括：
 * - 好友关系的查询、添加、删除
 * - 好友申请事件的创建、查询、删除
 * - 聊天会话的创建、删除、成员管理
 *
 * 所有方法均使用 ODB 事务保证数据一致性，异常时回滚并返回 false。
 */
class FriendTable {
public:
    /// 好友关系对象智能指针
    using RelationPtr = std::shared_ptr<friend_relation>;
    /// 好友申请事件对象智能指针
    using EventPtr = std::shared_ptr<friend_event>;
    /// 聊天会话对象智能指针
    using SessionPtr = std::shared_ptr<chat_session>;

    /**
     * @brief 构造函数
     * @param db ODB 数据库连接对象
     */
    FriendTable(std::shared_ptr<odb::database> db) : db_(db) {}
    ~FriendTable() {}

    // ==================== 好友关系操作 ====================

    /**
     * @brief 判断两个用户是否为好友关系
     * @param user_id   用户ID
     * @param friend_id 好友ID
     * @return true 表示已是好友，false 表示非好友或查询失败
     */
    bool is_friend(const std::string& user_id, const std::string& friend_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_relation> query;
            typedef odb::result<friend_relation> result;
            result r(db_->query<friend_relation>(
                query::user_id == user_id && query::friend_id == friend_id));
            bool found = (r.begin() != r.end());
            t.commit();
            return found;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] is_friend failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取指定用户的所有好友ID列表
     * @param user_id 用户ID
     * @return 好友ID列表，查询失败时返回空列表
     */
    std::vector<std::string> get_friend_ids(const std::string& user_id) {
        std::vector<std::string> ids;
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_relation> query;
            typedef odb::result<friend_relation> result;
            result r(db_->query<friend_relation>(query::user_id == user_id));
            for (const auto& rel : r) {
                ids.push_back(rel.friend_id());
            }
            t.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] get_friend_ids failed: {}", e.what());
        }
        return ids;
    }

    /**
     * @brief 添加双向好友关系
     *
     * 同时插入 (user_a -> user_b) 和 (user_b -> user_a) 两条记录，
     * 保证好友关系的双向性。在同一个事务中完成，要么全部成功，要么全部回滚。
     *
     * @param user_a 用户A的ID
     * @param user_b 用户B的ID
     * @return true 表示添加成功，false 表示失败
     */
    bool add_friend_relation(const std::string& user_a, const std::string& user_b) {
        try {
            odb::transaction t(db_->begin());
            friend_relation rel1(user_a, user_b);
            friend_relation rel2(user_b, user_a);
            db_->persist(rel1);
            db_->persist(rel2);
            t.commit();
            LOG_INFO("[FriendTable] add_friend_relation: {} <-> {}", user_a, user_b);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] add_friend_relation failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 删除双向好友关系
     *
     * 同时删除 (user_a -> user_b) 和 (user_b -> user_a) 两条记录。
     *
     * @param user_a 用户A的ID
     * @param user_b 用户B的ID
     * @return true 表示删除成功，false 表示失败
     */
    bool remove_friend_relation(const std::string& user_a, const std::string& user_b) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_relation> query;
            db_->erase_query<friend_relation>(
                query::user_id == user_a && query::friend_id == user_b);
            db_->erase_query<friend_relation>(
                query::user_id == user_b && query::friend_id == user_a);
            t.commit();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] remove_friend_relation failed: {}", e.what());
            return false;
        }
    }

    // ==================== 好友申请事件操作 ====================

    /**
     * @brief 根据事件ID获取好友申请事件
     * @param event_id 事件唯一标识
     * @return 事件对象智能指针，未找到时返回 nullptr
     */
    EventPtr get_event_by_id(const std::string& event_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_event> query;
            typedef odb::result<friend_event> result;
            result r(db_->query<friend_event>(query::event_id == event_id));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            EventPtr ev = std::make_shared<friend_event>(*r.begin());
            t.commit();
            return ev;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] get_event_by_id failed: {}", e.what());
            return nullptr;
        }
    }

    /**
     * @brief 检查是否存在待处理的好友申请
     *
     * 判断 req_user_id 是否已向 rsp_user_id 发送了尚未处理的好友申请。
     *
     * @param req_user_id 申请者用户ID
     * @param rsp_user_id 响应者用户ID
     * @return true 表示存在待处理申请，false 表示不存在或查询失败
     */
    bool has_pending_event(const std::string& req_user_id, const std::string& rsp_user_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_event> query;
            typedef odb::result<friend_event> result;
            result r(db_->query<friend_event>(
                query::req_user_id == req_user_id &&
                query::rsp_user_id == rsp_user_id &&
                query::status == fevent_status::PENDING));
            bool found = (r.begin() != r.end());
            t.commit();
            return found;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] has_pending_event failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 创建好友申请事件
     *
     * 新建一条状态为 PENDING 的好友申请记录。
     *
     * @param event_id     事件唯一标识（由调用方生成）
     * @param req_user_id  申请者用户ID
     * @param rsp_user_id  响应者用户ID
     * @return true 表示创建成功，false 表示失败
     */
    bool add_event(const std::string& event_id,
                   const std::string& req_user_id,
                   const std::string& rsp_user_id) {
        try {
            odb::transaction t(db_->begin());
            friend_event ev(event_id, req_user_id, rsp_user_id, fevent_status::PENDING);
            db_->persist(ev);
            t.commit();
            LOG_INFO("[FriendTable] add_event: {} -> {}", req_user_id, rsp_user_id);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] add_event failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 删除好友申请事件
     *
     * 在好友申请被处理（同意或拒绝）后调用，删除对应的事件记录。
     *
     * @param event_id 事件唯一标识
     * @return true 表示删除成功，false 表示失败
     */
    bool remove_event(const std::string& event_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_event> query;
            db_->erase_query<friend_event>(query::event_id == event_id);
            t.commit();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] remove_event failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取指定用户所有待处理的好友申请事件
     *
     * 查询所有发给 rsp_user_id 且状态为 PENDING 的事件，
     * 即"谁申请加我为好友"的列表。
     *
     * @param rsp_user_id 响应者（被申请者）用户ID
     * @return 待处理事件列表，查询失败时返回空列表
     */
    std::vector<EventPtr> get_pending_events(const std::string& rsp_user_id) {
        std::vector<EventPtr> events;
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<friend_event> query;
            typedef odb::result<friend_event> result;
            result r(db_->query<friend_event>(
                query::rsp_user_id == rsp_user_id &&
                query::status == fevent_status::PENDING));
            for (const auto& ev : r) {
                events.push_back(std::make_shared<friend_event>(ev));
            }
            t.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] get_pending_events failed: {}", e.what());
        }
        return events;
    }

    // ==================== 聊天会话操作 ====================

    /**
     * @brief 根据会话ID获取会话信息
     * @param session_id 会话唯一标识
     * @return 会话对象智能指针，未找到时返回 nullptr
     */
    SessionPtr get_session_by_id(const std::string& session_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<chat_session> query;
            typedef odb::result<chat_session> result;
            result r(db_->query<chat_session>(query::session_id == session_id));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            SessionPtr s = std::make_shared<chat_session>(*r.begin());
            t.commit();
            return s;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] get_session_by_id failed: {}", e.what());
            return nullptr;
        }
    }

    /**
     * @brief 创建新的聊天会话
     *
     * @param session_id   会话唯一标识（由调用方生成）
     * @param session_name 会话名称（群聊使用，单聊可为空）
     * @param type         会话类型（SINGLE 单聊 / GROUP 群聊）
     * @return true 表示创建成功，false 表示失败
     */
    bool add_session(const std::string& session_id,
                    const std::string& session_name,
                    session_type_t type) {
        try {
            odb::transaction t(db_->begin());
            chat_session s(session_id, session_name, type);
            db_->persist(s);
            t.commit();
            LOG_INFO("[FriendTable] add_session: {}", session_id);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] add_session failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 删除聊天会话
     * @param session_id 会话唯一标识
     * @return true 表示删除成功，false 表示失败
     */
    bool remove_session(const std::string& session_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<chat_session> query;
            db_->erase_query<chat_session>(query::session_id == session_id);
            t.commit();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] remove_session failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取指定用户参与的所有会话ID列表
     * @param user_id 用户ID
     * @return 会话ID列表，查询失败时返回空列表
     */
    std::vector<std::string> get_session_ids_by_user(const std::string& user_id) {
        std::vector<std::string> ids;
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<chat_session_member> query;
            typedef odb::result<chat_session_member> result;
            result r(db_->query<chat_session_member>(query::user_id == user_id));
            for (const auto& m : r) {
                ids.push_back(m.session_id());
            }
            t.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] get_session_ids_by_user failed: {}", e.what());
        }
        return ids;
    }

    /**
     * @brief 批量添加会话成员
     *
     * 将多个用户加入指定会话，在单个事务中完成。
     *
     * @param session_id 会话唯一标识
     * @param user_ids   待添加的用户ID列表
     * @return true 表示全部添加成功，false 表示失败
     */
    bool add_session_members(const std::string& session_id,
                            const std::vector<std::string>& user_ids) {
        try {
            odb::transaction t(db_->begin());
            for (const auto& uid : user_ids) {
                chat_session_member m(session_id, uid);
                db_->persist(m);
            }
            t.commit();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] add_session_members failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 删除指定会话的所有成员
     *
     * 在删除会话时调用，先清空成员关联表。
     *
     * @param session_id 会话唯一标识
     * @return true 表示删除成功，false 表示失败
     */
    bool remove_all_session_members(const std::string& session_id) {
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<chat_session_member> query;
            db_->erase_query<chat_session_member>(query::session_id == session_id);
            t.commit();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] remove_all_session_members failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取指定会话的所有成员ID列表
     * @param session_id 会话唯一标识
     * @return 成员用户ID列表，查询失败时返回空列表
     */
    std::vector<std::string> get_session_member_ids(const std::string& session_id) {
        std::vector<std::string> ids;
        try {
            odb::transaction t(db_->begin());
            typedef odb::query<chat_session_member> query;
            typedef odb::result<chat_session_member> result;
            result r(db_->query<chat_session_member>(query::session_id == session_id));
            for (const auto& m : r) {
                ids.push_back(m.user_id());
            }
            t.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("[FriendTable] get_session_member_ids failed: {}", e.what());
        }
        return ids;
    }

private:
    std::shared_ptr<odb::database> db_;  ///< ODB 数据库连接对象
};

} // namespace friend_table

#endif
