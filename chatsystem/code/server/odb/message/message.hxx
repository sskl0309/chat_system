// =============================================================================
// message.hxx - 消息信息 ODB 持久化类定义
// =============================================================================
// 本文件定义消息存储子服务的消息数据表对应的 ODB 持久化类。
// 通过 ODB 编译器生成数据库支持代码，实现关系型数据库中消息数据的操作。
//
// ODB 代码生成命令：
//   odb -d mysql --generate-query --generate-schema --std c++11 message.hxx
//
// 数据表字段：
//   1. _id            - 主键ID，自动生成
//   2. _message_id    - 消息唯一标识
//   3. _created_time  - 消息产生时间（字符串格式 'YYYY-MM-DD HH:MM:SS'）
//   4. _from_user_id  - 消息发送者用户ID
//   5. _to_session_id - 消息所属会话ID
//   6. _message_type  - 消息类型 (0=文本, 1=图片, 2=文件, 3=语音)
//   7. _content       - 文本消息内容（仅文本消息存储）
//   8. _file_id       - 文件ID（仅文件/语音/图片消息）
//   9. _filename      - 文件名称（仅文件消息）
//   10. _filesize     - 文件大小（仅文件/语音/图片消息）
// =============================================================================

#ifndef MESSAGE_HXX
#define MESSAGE_HXX

#include <string>
#include <cstddef>
#include <odb/core.hxx>
#include <odb/nullable.hxx>

#pragma db object
class msg_record
{
public:
    msg_record() {}

    explicit msg_record(const std::string& message_id)
        : _message_id(message_id) {}

    msg_record(const std::string& message_id,
            const std::string& created_time,
            const std::string& from_user_id,
            const std::string& to_session_id,
            signed char message_type)
        : _message_id(message_id),
          _created_time(created_time),
          _from_user_id(from_user_id),
          _to_session_id(to_session_id),
          _message_type(message_type) {}

    unsigned long id() const { return _id; }
    void id(unsigned long val) { _id = val; }

    const std::string& message_id() const { return _message_id; }
    void message_id(const std::string& val) { _message_id = val; }

    const std::string& created_time() const { return _created_time; }
    void created_time(const std::string& val) { _created_time = val; }

    const odb::nullable<std::string>& from_user_id() const { return _from_user_id; }
    void from_user_id(const std::string& val) { _from_user_id = val; }
    void from_user_id(const odb::nullable<std::string>& val) { _from_user_id = val; }

    const odb::nullable<std::string>& to_session_id() const { return _to_session_id; }
    void to_session_id(const std::string& val) { _to_session_id = val; }
    void to_session_id(const odb::nullable<std::string>& val) { _to_session_id = val; }

    signed char message_type() const { return _message_type; }
    void message_type(signed char val) { _message_type = val; }

    const odb::nullable<std::string>& content() const { return _content; }
    void content(const std::string& val) { _content = val; }
    void content(const odb::nullable<std::string>& val) { _content = val; }

    const odb::nullable<std::string>& file_id() const { return _file_id; }
    void file_id(const std::string& val) { _file_id = val; }
    void file_id(const odb::nullable<std::string>& val) { _file_id = val; }

    const odb::nullable<std::string>& filename() const { return _filename; }
    void filename(const std::string& val) { _filename = val; }
    void filename(const odb::nullable<std::string>& val) { _filename = val; }

    const odb::nullable<unsigned long>& filesize() const { return _filesize; }
    void filesize(unsigned long val) { _filesize = val; }
    void filesize(const odb::nullable<unsigned long>& val) { _filesize = val; }

private:
    friend class odb::access;

    #pragma db id auto
    unsigned long _id;

    #pragma db unique type("VARCHAR(127)")
    std::string _message_id;

    #pragma db type("VARCHAR(31)") not_null
    std::string _created_time;

    #pragma db type("VARCHAR(127)")
    odb::nullable<std::string> _from_user_id;

    #pragma db type("VARCHAR(127)")
    odb::nullable<std::string> _to_session_id;

    #pragma db not_null
    signed char _message_type;

    odb::nullable<std::string> _content;

    #pragma db type("VARCHAR(127)")
    odb::nullable<std::string> _file_id;

    #pragma db type("VARCHAR(127)")
    odb::nullable<std::string> _filename;

    odb::nullable<unsigned long> _filesize;
};

#endif // MESSAGE_HXX