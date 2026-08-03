// =============================================================================
// friend_relation.hxx - 好友关系表 ODB 映射定义
// =============================================================================
// 本文件定义好友关系的 ODB ORM 映射结构，对应数据库中的 friend_relation 表。
//
// 设计说明：
//   - 记录 A <-> B 两位用户之间的好友关系，需要双向存储 (A,B) 与 (B,A)
//   - 仅包含两个用户ID字段，不冗余存储其他用户信息
//
// 字段设计：
//   - _id:        自增主键
//   - _user_id:   用户ID（建立关系的一方）
//   - _friend_id: 好友ID
// =============================================================================

#pragma once

#include <string>
#include <odb/core.hxx>

/**
 * @brief 好友关系 ODB 映射类
 *
 * 映射数据库表：friend_relation
 */
#pragma db object
class friend_relation {
public:
    friend_relation() {}

    friend_relation(const std::string& user_id, const std::string& friend_id)
        : _user_id(user_id), _friend_id(friend_id) {}

    const std::string& user_id() const { return _user_id; }
    void user_id(const std::string& val) { _user_id = val; }

    const std::string& friend_id() const { return _friend_id; }
    void friend_id(const std::string& val) { _friend_id = val; }

private:
    friend class odb::access;

    #pragma db id auto
    long int _id;

    #pragma db index type("VARCHAR(127)")
    std::string _user_id;

    #pragma db type("VARCHAR(127)")
    std::string _friend_id;
};
