// =============================================================================
// friend_event.hxx - 好友申请事件表 ODB 映射定义
// =============================================================================
// 本文件定义好友申请事件的 ODB ORM 映射结构，对应数据库中的 friend_event 表。
//
// 设计说明：
//   - 记录一次完整的好友申请流程，状态机：PENDING -> ACCEPT / REJECT
//   - 处理完毕后应删除事件记录
//
// 字段设计：
//   - _id:           自增主键
//   - _event_id:     事件唯一标识
//   - _req_user_id:  申请者用户ID
//   - _rsp_user_id:  响应者用户ID
//   - _status:       事件状态 (PENDING / ACCEPT / REJECT)
// =============================================================================

#pragma once

#include <string>
#include <odb/core.hxx>

/**
 * @brief 好友申请事件状态枚举
 */
enum class fevent_status {
    PENDING = 1,   ///< 待处理
    ACCEPT = 2,    ///< 已同意
    REJECT = 3     ///< 已拒绝
};

/**
 * @brief 好友申请事件 ODB 映射类
 *
 * 映射数据库表：friend_event
 */
#pragma db object
class friend_event {
public:
    friend_event() {}

    friend_event(const std::string& event_id,
                 const std::string& req_user_id,
                 const std::string& rsp_user_id,
                 fevent_status status = fevent_status::PENDING)
        : _event_id(event_id),
          _req_user_id(req_user_id),
          _rsp_user_id(rsp_user_id),
          _status(status) {}

    const std::string& event_id() const { return _event_id; }
    void event_id(const std::string& val) { _event_id = val; }

    const std::string& req_user_id() const { return _req_user_id; }
    void req_user_id(const std::string& val) { _req_user_id = val; }

    const std::string& rsp_user_id() const { return _rsp_user_id; }
    void rsp_user_id(const std::string& val) { _rsp_user_id = val; }

    fevent_status status() const { return _status; }
    void status(fevent_status val) { _status = val; }

private:
    friend class odb::access;

    #pragma db id auto
    long int _id;

    #pragma db unique type("VARCHAR(127)")
    std::string _event_id;

    #pragma db type("VARCHAR(127)")
    std::string _req_user_id;

    #pragma db type("VARCHAR(127)")
    std::string _rsp_user_id;

    #pragma db type("TINYINT")
    fevent_status _status;
};
