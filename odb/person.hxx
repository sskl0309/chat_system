#pragma once
#include <string>
#include <cstddef>
#include <boost/date_time/posix_time/posix_time.hpp>

// 使用 ODB 将类声明为持久化类，需要包含 ODB 的核心头文件
// #pragma db object 指示 ODB 编译器将 Person 类视为一个持久化类
#include <odb/core.hxx>

typedef boost::posix_time::ptime ptime;

#pragma db object
class Person {
public:
    Person(const std::string& name, int age, const ptime& update)
        : _name(name), _age(age), _update(update)
    {
    }

    void age(int val) { _age = val; }
    int age() { return _age; }

    void name(const std::string& val) { _name = val; }
    std::string name() { return _name; }

    void update(const ptime& update) { _update = update; }
    std::string update()
    {
        return boost::posix_time::to_simple_string(_update);
    }

private:
    // 将 odb::access 类作为 Person 类的朋友
    // 这是使数据库支持代码可访问默认构造函数和数据成员所必需的
    // 如果类具有公共默认构造函数和公共数据成员或数据成员的公共访问器
    // 和修饰符，则不需要友元声明
    friend class odb::access;
    Person() {}

    // #pragma db id auto 表示 ID 字段将自动生成（通常是数据库中的主键）
#pragma db id auto
    unsigned long _id;

    unsigned short _age;
    std::string _name;

#pragma db type("TIMESTAMP") not_null
    boost::posix_time::ptime _update;
};

// 将 ODB 编译指示组合在一起，并放在类定义之后
// 它们也可以移动到一个单独的标头中，使原始类完全保持不变
// #pragma db object(person)
// #pragma db member(person::_name) id

// 使用 ODB 编译器生成数据库支持代码：
// odb -d mysql --generate-query --generate-schema person.hxx
// 如果用到了 boost 库中的接口，需要使用选项 --profile boost/date-time：
// odb -d mysql --generate-query --generate-schema --profile boost/date-time person.hxx
