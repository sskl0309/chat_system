// =============================================================================
// chat_session_member.hxx - 会话成员表 ODB 映射定义
// =============================================================================
// 本文件定义会话成员的 ODB ORM 映射结构，对应数据库中的 chat_session_member 表。
//
// 设计目的：
//   建立用户与会话之间的多对多关系，明确哪个用户属于哪个会话。
//   通过此表可以：
//     1. 根据会话ID获取所有成员
//     2. 根据用户ID获取所属的所有会话
//
// 字段设计：
//   - _id:         自增主键，ODB 内部使用
//   - _session_id: 会话标识（索引）
//   - _user_id:    用户标识
//
// 使用方式：
//   1. 通过 ODB 编译器生成代码：odb -d mysql --generate-query --generate-schema chat_session_member.hxx
//   2. 生成的文件：chat_session_member-odb.hxx, chat_session_member-odb.cxx, chat_session_member.sql
// =============================================================================

#pragma once

#include <odb/core.hxx>
#include <string>

/**
 * @brief 会话成员 ODB 映射类
 * 
 * 映射数据库表：chat_session_member
 * 用于存储会话与用户之间的关联关系，支持单聊和群聊场景。
 * 
 * 单聊会话包含两个成员，群聊会话包含三个及以上成员。
 */
#pragma db object
class chat_session_member {
public:
    /**
     * @brief 默认构造函数
     * 
     * ODB 需要默认构造函数来创建对象实例。
     */
    chat_session_member() {}

    /**
     * @brief 带参数构造函数（业务层使用）
     * 
     * @param session_id 会话唯一标识
     * @param user_id    用户唯一标识
     */
    chat_session_member(const std::string& session_id, const std::string& user_id)
        : _session_id(session_id), _user_id(user_id) {}

    /**
     * @brief 获取会话ID
     * @return 会话唯一标识
     */
    const std::string& session_id() const { return _session_id; }

    /**
     * @brief 获取用户ID
     * @return 用户唯一标识
     */
    const std::string& user_id() const { return _user_id; }

private:
    friend class odb::access;  ///< ODB 需要访问私有成员

    #pragma db id auto                              ///< 自增主键
    unsigned long _id;                              ///< 数据库主键（内部使用）

    #pragma db index type("VARCHAR(127)")           ///< 索引，加快会话查询
    std::string _session_id;                        ///< 会话唯一标识

    #pragma db type("VARCHAR(127)")                 ///< 用户标识字段类型
    std::string _user_id;                           ///< 用户唯一标识
};
