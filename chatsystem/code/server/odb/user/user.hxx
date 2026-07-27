// =============================================================================
// user.hxx - 用户信息 ODB 持久化类定义
// =============================================================================
// 本文件定义用户子服务的用户信息数据表对应的 ODB 持久化类。
// 通过 ODB 编译器生成数据库支持代码，实现关系型数据库中用户数据的操作。
//
// ODB 代码生成命令：
//   odb -d mysql --generate-query --generate-schema --std c++11 user.hxx
//
// 数据表字段：
//   1. _id         - 主键ID，自动生成
//   2. _user_id    - 用户唯一标识
//   3. _nickname   - 用户昵称，可用作登录用户名
//   4. _passwd     - 登录密码
//   5. _avatar_id  - 用户头像文件ID
//   6. _email      - 绑定邮箱
//   7. _description- 用户签名
// =============================================================================

#ifndef USER_HXX
#define USER_HXX

#include <string>
#include <cstddef>
#include <odb/core.hxx>
#include <odb/nullable.hxx>

// #pragma db object: 声明该类为 ODB 持久化对象
// 每个持久化类必须有默认构造函数，且成员变量需满足 ODB 类型要求
#pragma db object
class user {
public:
    user() {}

    /**
     * @brief 构造函数（注册时使用）
     * @param user_id 用户唯一标识
     */
    explicit user(const std::string& user_id)
        : _user_id(user_id) {}

    /**
     * @brief 构造函数（带昵称和密码）
     * @param user_id 用户唯一标识
     * @param nickname 用户昵称
     * @param passwd 登录密码
     */
    user(const std::string& user_id, const std::string& nickname, const std::string& passwd)
        : _user_id(user_id), _nickname(nickname), _passwd(passwd) {}

// ==================== getter / setter ====================
// ODB 要求持久化类提供所有字段的 getter/setter，其中：
//   - auto id 字段只需 setter（数据库自动生成）
//   - nullable 字段需提供两种 setter（值类型 + nullable 类型）

unsigned long id() const { return _id; }
void id(unsigned long val) { _id = val; }

const std::string& user_id() const { return _user_id; }
void user_id(const std::string& val) { _user_id = val; }

const odb::nullable<std::string>& nickname() const { return _nickname; }
void nickname(const std::string& val) { _nickname = val; }
void nickname(const odb::nullable<std::string>& val) { _nickname = val; }

const odb::nullable<std::string>& passwd() const { return _passwd; }
void passwd(const std::string& val) { _passwd = val; }
void passwd(const odb::nullable<std::string>& val) { _passwd = val; }

const odb::nullable<std::string>& avatar_id() const { return _avatar_id; }
void avatar_id(const std::string& val) { _avatar_id = val; }
void avatar_id(const odb::nullable<std::string>& val) { _avatar_id = val; }

const odb::nullable<std::string>& email() const { return _email; }
void email(const std::string& val) { _email = val; }
void email(const odb::nullable<std::string>& val) { _email = val; }

const odb::nullable<std::string>& description() const { return _description; }
void description(const std::string& val) { _description = val; }
void description(const odb::nullable<std::string>& val) { _description = val; }

private:
    friend class odb::access;   // ODB 框架需要访问私有成员

    // #pragma db 指令说明：
    //   id auto   - 自增主键，由数据库自动生成
    //   unique    - 创建唯一索引，禁止重复值
    //   type("..")- 指定数据库列类型，覆盖 ODB 默认类型映射
    //   nullable  - 允许字段为 NULL（使用 odb::nullable<T> 包装）

    #pragma db id auto
    unsigned long _id;                              // 自增主键

    #pragma db unique type("VARCHAR(127)")
    std::string _user_id;                           // 用户唯一标识（唯一索引）

    #pragma db unique type("VARCHAR(63)")
    odb::nullable<std::string> _nickname;           // 昵称（唯一，可为空）

    #pragma db type("VARCHAR(255)")
    odb::nullable<std::string> _passwd;             // 密码（可为空）

    #pragma db type("VARCHAR(127)")
    odb::nullable<std::string> _avatar_id;          // 头像文件ID（可为空）

    #pragma db unique type("VARCHAR(255)")
    odb::nullable<std::string> _email;              // 邮箱（唯一，可为空）

    #pragma db type("VARCHAR(255)")
    odb::nullable<std::string> _description;        // 用户签名（可为空）
};

// ODB 查询特化，便于通过字段名构建查询条件
// 生成命令：odb -d mysql --generate-query --generate-schema --std c++11 user.hxx

#endif // USER_HXX
