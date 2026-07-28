// =============================================================================
// message_table.hpp - 消息数据数据库操作模块
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

class MessageTable {
public:
    using MessagePtr = std::shared_ptr<msg_record>;

    MessageTable(std::shared_ptr<odb::database> db) : db_(db) {}

    ~MessageTable() {}

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

    MessagePtr select_by_message_id(const std::string& message_id) {
        try {
            typedef odb::query<msg_record> query;
            typedef odb::result<msg_record> result;

            odb::transaction t(db_->begin());
            result r(db_->query<msg_record>(query::message_id == message_id));
            if (r.begin() == r.end()) {
                t.commit();
                return nullptr;
            }
            MessagePtr msg = std::make_shared<msg_record>(*r.begin());
            t.commit();
            LOG_INFO("[MessageTable] Select by message_id: {}", message_id);
            return msg;
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select by message_id failed: {}", e.what());
            return nullptr;
        }
    }

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

    std::vector<MessagePtr> select_by_time_range(const std::string& session_id,
                                                  const std::string& start_time,
                                                  const std::string& end_time) {
        std::vector<MessagePtr> result_list;
        try {
            odb::transaction t(db_->begin());

            typedef odb::query<msg_record> query;
            auto result = db_->query<msg_record>(
                query::to_session_id == session_id &&
                query::created_time >= start_time &&
                query::created_time <= end_time);

            for (const auto& msg : result) {
                result_list.push_back(std::make_shared<msg_record>(msg));
            }

            // Sort by created_time ascending
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

    std::vector<MessagePtr> select_recent(const std::string& session_id, std::size_t count) {
        std::vector<MessagePtr> result_list;
        try {
            odb::transaction t(db_->begin());

            typedef odb::query<msg_record> query;
            auto result = db_->query<msg_record>(
                query::to_session_id == session_id);

            // Collect all messages and sort by time descending
            for (const auto& msg : result) {
                result_list.push_back(std::make_shared<msg_record>(msg));
            }

            // Sort by created_time descending, then take first 'count'
            std::sort(result_list.begin(), result_list.end(),
                [](const MessagePtr& a, const MessagePtr& b) {
                    return a->created_time() > b->created_time();
                });

            if (result_list.size() > count) {
                result_list.resize(count);
            }

            // Reverse to ascending order for display
            std::reverse(result_list.begin(), result_list.end());

            t.commit();
            LOG_INFO("[MessageTable] Select {} recent messages, session: {}",
                     result_list.size(), session_id);
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageTable] Select recent failed: {}", e.what());
        }
        return result_list;
    }

    std::vector<MessagePtr> select_recent_before(const std::string& session_id,
                                                  std::size_t count,
                                                  const std::string& before_time) {
        std::vector<MessagePtr> result_list;
        try {
            odb::transaction t(db_->begin());

            typedef odb::query<msg_record> query;
            auto result = db_->query<msg_record>(
                query::to_session_id == session_id &&
                query::created_time <= before_time);

            for (const auto& msg : result) {
                result_list.push_back(std::make_shared<msg_record>(msg));
            }

            // Sort by created_time descending, then take first 'count'
            std::sort(result_list.begin(), result_list.end(),
                [](const MessagePtr& a, const MessagePtr& b) {
                    return a->created_time() > b->created_time();
                });

            if (result_list.size() > count) {
                result_list.resize(count);
            }

            // Reverse to ascending order for display
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
    std::shared_ptr<odb::database> db_;
};

} // namespace message_table

#endif // MESSAGE_TABLE_HPP