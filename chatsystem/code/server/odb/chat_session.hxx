// =============================================================================
// chat_session.hxx - 聊天会话表 ODB 映射定义
// =============================================================================
// 本文件定义聊天会话的 ODB ORM 映射结构，对应数据库中的 chat_session 表。
//
// 会话类型说明：
//   - SINGLE: 单聊会话（由服务器在好友同意时自动创建）
//   - GROUP:  群聊会话（由用户手动创建）
//
// 字段设计：
//   - _id:          自增主键，ODB 内部使用
//   - _session_id:  会话唯一标识，业务层使用（唯一索引）
//   - _session_name: 会话名称（群聊使用，单聊可空）
//   - _session_type: 会话类型（单聊/群聊）
//
// 使用方式：
//   1. 通过 ODB 编译器生成代码：odb -d mysql --generate-query --generate-schema chat_session.hxx
//   2. 生成的文件：chat_session-odb.hxx, chat_session-odb.cxx, chat_session.sql
// =============================================================================

#pragma once

#include <odb/core.hxx>
#include <odb/nullable.hxx>
#include <string>

/**
 * @brief 会话类型枚举
 * 
 * SINGLE: 单聊会话（两人之间的聊天）
 * GROUP:  群聊会话（三人及以上的聊天）
 */
enum class session_type_t {
    SINGLE = 1,  ///< 单聊会话
    GROUP = 2    ///< 群聊会话
};

/**
 * @brief 聊天会话 ODB 映射类
 * 
 * 映射数据库表：chat_session
 * 用于存储聊天会话的基本信息，包括会话ID、会话名称和会话类型。
 * 单聊会话由服务器在好友同意时自动创建，群聊会话由用户手动创建。
 */
#pragma db object
class chat_session {
public:
    /**
     * @brief 默认构造函数
     * 
     * ODB 需要默认构造函数来创建对象实例。
     */
    chat_session() {}

    /**
     * @brief 带参数构造函数（业务层使用）
     * 
     * @param session_id    会话唯一标识
     * @param session_name  会话名称（群聊使用）
     * @param session_type  会话类型
     */
    chat_session(const std::string& session_id, 
                 const std::string& session_name, 
                 session_type_t session_type)
        : _session_id(session_id), _session_name(session_name), _session_type(session_type) {}

    /**
     * @brief 获取会话ID
     * @return 会话唯一标识
     */
    const std::string& session_id() const { return _session_id; }

    /**
     * @brief 获取会话名称
     * @return 会话名称（可能为空，单聊会话为空）
     */
    const odb::nullable<std::string>& session_name() const { return _session_name; }

    /**
     * @brief 获取会话类型
     * @return 会话类型（单聊/群聊）
     */
    session_type_t session_type() const { return _session_type; }

    /**
     * @brief 设置会话名称
     * @param name 会话名称
     */
    void session_name(const std::string& name) { _session_name = name; }

    /**
     * @brief 设置会话类型
     * @param type 会话类型
     */
    void session_type(session_type_t type) { _session_type = type; }

private:
    friend class odb::access;  ///< ODB 需要访问私有成员

    #pragma db id auto                              ///< 自增主键
    long int _id;                                   ///< 数据库主键（内部使用）

    #pragma db unique type("VARCHAR(127)")          ///< 唯一索引，业务层会话标识
    std::string _session_id;                        ///< 会话唯一标识

    #pragma db type("VARCHAR(127)")                 ///< 会话名称字段类型
    odb::nullable<std::string> _session_name;       ///< 会话名称（群聊使用，单聊可为空）

    #pragma db type("TINYINT")                      ///< 会话类型字段类型（1字节）
    session_type_t _session_type;                   ///< 会话类型（SINGLE/GROUP）
};
