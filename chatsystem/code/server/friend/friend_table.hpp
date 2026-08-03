// =============================================================================
// friend_table.hpp - 好友管理数据数据库操作模块
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

class FriendTable {
public:
    using RelationPtr = std::shared_ptr<friend_relation>;
    using EventPtr = std::shared_ptr<friend_event>;
    using SessionPtr = std::shared_ptr<chat_session>;

    FriendTable(std::shared_ptr<odb::database> db) : db_(db) {}
    ~FriendTable() {}

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
    std::shared_ptr<odb::database> db_;
};

} // namespace friend_table

#endif
