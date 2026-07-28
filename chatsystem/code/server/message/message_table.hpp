// =============================================================================
// message_table.hpp - 消息数据数据库操作模块
// =============================================================================
// 本模块提供消息数据的 MySQL 数据库 CRUD 操作，基于 ODB ORM 框架实现。
// 所有方法均为内联实现（header-only），直接在头文件中定义。
//
// 功能列表：
//   1. insert           - 新增一条消息记录
//   2. select_by_message_id   - 根据消息ID查询单条消息
//   3. select_by_message_ids  - 根据消息ID列表批量查询消息
//   4. select_by_time_range   - 根据时间范围查询消息
//   5. select_recent          - 查询最近N条消息
//   6. select_recent_before   - 查询指定时间之前的N条消息
//
// 注意：
//   - 时间字段使用字符串格式 'YYYY-MM-DD HH:MM:SS' 存储
//   - 排序采用内存排序（std::sort），避免 ODB 版本兼容性问题
//   - 使用 shared_ptr 管理对象生命周期
// =============================================================================

#ifndef MESSAGE_TABLE_HPP
#define MESSAGE_TABLE_HPP

#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <odb/session.hxx>
#include <odb/query.hxx>
#include <odb/result.hxx>

#include "message-odb.hxx"
#include "../common/log.hpp"

namespace message_table {

/**
 * @brief 消息数据操作类
 *
 * 提供对 msg_record 表的完整 CRUD 操作封装。
 * 所有操作均在事务中执行，保证数据一致性。
 */
class MessageTable {
public:
    // 消息记录智能指针类型别名
    using MessagePtr = std::shared_ptr<msg_record>;

    // 构造函数，接收 ODB 数据库实例
    MessageTable(std::shared_ptr<odb::database> db) : db_(db) {}

    ~MessageTable() {}

    /**
     * @brief 新增一条消息记录
     *
     * @param msg 待持久化的消息对象（引用方式传入，持久化后对象ID会被自动填充）
     * @return true  插入成功
     * @return false 插入失败（消息已存在或数据库异常）
     */
    bool insert(msg_record& msg) {
        try {
            odb::transaction t(db_->begin());
            db_->persist(msg);
            t.commit();
            LOG_INFO("[MessageTable] Insert message success, message_id: {}", msg.message_id());
            return true;
        } catch (const odb::object_already_persistent& e) {
            LOG_WARN("[MessageTable] Message already exists: {}", e.what());
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Insert message failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 根据消息ID查询单条消息
     *
     * @param message_id 消息唯一标识
     * @return MessagePtr 找到返回消息指针，未找到返回 nullptr
     */
    MessagePtr select_by_message_id(const std::string& message_id) {
        try {
            typedef odb::query<msg_record> query;
            typedef odb::result<msg_record> result;

            odb::transaction t(db_->begin());
            // 使用 ODB 查询语法：query::字段名 == 值
            result r(db_->query<msg_record>(query::message_id == message_id));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            // 查询结果通过 make_shared 拷贝构造为独立对象
            MessagePtr msg = std::make_shared<msg_record>(*r.begin());
            t.commit();
            LOG_INFO("[MessageTable] Select by message_id: {}", message_id);
            return msg;
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select by message_id failed: {}", e.what());
            return nullptr;
        }
    }

    /**
     * @brief 根据消息ID列表批量查询消息
     *
     * @param message_ids 待查询的消息ID数组
     * @return std::vector<MessagePtr> 查询到的消息列表（顺序与输入ID顺序无关）
     */
    std::vector<MessagePtr> select_by_message_ids(const std::vector<std::string>& message_ids) {
        std::vector<MessagePtr> result_list;
        if (message_ids.empty()) return result_list;

        try {
            odb::transaction t(db_->begin());
            for (const auto& mid : message_ids) {
                typedef odb::query<msg_record> query;
                typedef odb::result<msg_record> res_type;
                res_type r(db_->query<msg_record>(query::message_id == mid));
                auto it = r.begin();
                if (it != r.end()) {
                    result_list.push_back(std::make_shared<msg_record>(*it));
                }
            }
            t.commit();
            LOG_INFO("[MessageTable] Select {} messages by IDs", result_list.size());
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select by message_ids failed: {}", e.what());
        }
        return result_list;
    }

    /**
     * @brief 根据会话ID和时间范围查询消息
     *
     * @param session_id 会话ID
     * @param start_time 起始时间（格式: 'YYYY-MM-DD HH:MM:SS'）
     * @param end_time   结束时间（格式: 'YYYY-MM-DD HH:MM:SS'）
     * @return std::vector<MessagePtr> 按时间升序排列的消息列表
     */
    std::vector<MessagePtr> select_by_time_range(const std::string& session_id,
                                                  const std::string& start_time,
                                                  const std::string& end_time) {
        std::vector<MessagePtr> result_list;
        try {
            odb::transaction t(db_->begin());

            typedef odb::query<msg_record> query;
            // 组合查询条件：会话ID匹配 AND 时间 >= 起始 AND 时间 <= 结束
            auto result = db_->query<msg_record>(
                query::to_session_id == session_id &&
                query::created_time >= start_time &&
                query::created_time <= end_time);

            for (const auto& msg : result) {
                result_list.push_back(std::make_shared<msg_record>(msg));
            }

            // 内存排序：按创建时间升序排列（早的在前）
            std::sort(result_list.begin(), result_list.end(),
                [](const MessagePtr& a, const MessagePtr& b) {
                    return a->created_time() < b->created_time();
                });

            t.commit();
            LOG_INFO("[MessageTable] Select {} messages by time range, session: {}",
                     result_list.size(), session_id);
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select by time range failed: {}", e.what());
        }
        return result_list;
    }

    /**
     * @brief 查询指定会话最近的N条消息
     *
     * @param session_id 会话ID
     * @param count      要获取的消息数量
     * @return std::vector<MessagePtr> 按时间升序排列的最近N条消息
     */
    std::vector<MessagePtr> select_recent(const std::string& session_id, std::size_t count) {
        std::vector<MessagePtr> result_list;
        try {
            odb::transaction t(db_->begin());

            typedef odb::query<msg_record> query;
            // 查询该会话的所有消息
            auto result = db_->query<msg_record>(
                query::to_session_id == session_id);

            // 收集所有消息到内存
            for (const auto& msg : result) {
                result_list.push_back(std::make_shared<msg_record>(msg));
            }

            // 第一步：按时间降序排列（最新的在前）
            std::sort(result_list.begin(), result_list.end(),
                [](const MessagePtr& a, const MessagePtr& b) {
                    return a->created_time() > b->created_time();
                });

            // 第二步：截取前 count 条
            if (result_list.size() > count) {
                result_list.resize(count);
            }

            // 第三步：反转为升序（早的在前），方便前端从上往下展示
            std::reverse(result_list.begin(), result_list.end());

            t.commit();
            LOG_INFO("[MessageTable] Select {} recent messages, session: {}",
                     result_list.size(), session_id);
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select recent failed: {}", e.what());
        }
        return result_list;
    }

    /**
     * @brief 查询指定会话中，指定时间之前的最近N条消息
     *
     * 用于"加载更多历史消息"场景，用户向上滚动时加载更早的消息。
     *
     * @param session_id  会话ID
     * @param count       要获取的消息数量
     * @param before_time 截止时间（只获取此时间之前的消息）
     * @return std::vector<MessagePtr> 按时间升序排列的消息列表
     */
    std::vector<MessagePtr> select_recent_before(const std::string& session_id,
                                                  std::size_t count,
                                                  const std::string& before_time) {
        std::vector<MessagePtr> result_list;
        try {
            odb::transaction t(db_->begin());

            typedef odb::query<msg_record> query;
            // 查询条件：会话ID匹配 AND 时间 <= 截止时间
            auto result = db_->query<msg_record>(
                query::to_session_id == session_id &&
                query::created_time <= before_time);

            for (const auto& msg : result) {
                result_list.push_back(std::make_shared<msg_record>(msg));
            }

            // 与 select_recent 相同的三步排序逻辑
            std::sort(result_list.begin(), result_list.end(),
                [](const MessagePtr& a, const MessagePtr& b) {
                    return a->created_time() > b->created_time();
                });

            if (result_list.size() > count) {
                result_list.resize(count);
            }

            std::reverse(result_list.begin(), result_list.end());

            t.commit();
            LOG_INFO("[MessageTable] Select {} recent messages before time, session: {}",
                     result_list.size(), session_id);
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select recent before failed: {}", e.what());
        }
        return result_list;
    }

private:
    // ODB 数据库实例（共享所有权）
    std::shared_ptr<odb::database> db_;
};

} // namespace message_table

#endif // MESSAGE_TABLE_HPP