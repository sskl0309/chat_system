// =============================================================================
// message_service_impl.hpp - 消息存储服务 RPC 接口实现声明
// =============================================================================
// 本文件声明消息存储服务的核心实现类 MsgStorageServiceImpl，
// 继承自 protobuf 生成的 MsgStorageService 基类，实现三个对外 RPC 接口。
//
// 对外接口：
//   1. GetRecentMsg    - 获取最近N条消息（登录后打开聊天框显示历史）
//   2. GetHistoryMsg   - 获取指定时间段的消息（按时间范围搜索）
//   3. MsgSearch       - 关键字消息搜索（基于 ES + MySQL 回查）
//
// 内部功能：
//   - on_message_consume  - MQ 消息消费回调，处理消息存储
//   - construct_message_info - 从数据库记录构造完整消息对象
//   - get_user_info      - 调用用户子服务获取发送者信息
//   - get_file_data      - 调用文件子服务获取文件数据
//   - store_file_message - 将文件数据转储到文件子服务
// =============================================================================

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "message.pb.h"
#include "message_table.hpp"
#include "message_es.hpp"
#include "../common/brpc_client.hpp"
#include "../common/mq_client.hpp"

namespace message {

/**
 * @brief 时间工具函数命名空间
 *
 * 处理毫秒时间戳与字符串格式的互转，
 * 用于 protobuf 消息时间（毫秒）与 MySQL 存储时间（字符串）的转换。
 */
namespace time_util {

/**
 * @brief 毫秒时间戳转字符串
 *
 * @param ms 毫秒时间戳（如 1722172800000）
 * @return std::string 格式化时间字符串（如 "2024-07-28 10:00:00"）
 */
inline std::string ms_to_string(int64_t ms) {
    time_t seconds = ms / 1000;
    std::time_t t = seconds;
    std::tm tm_time;
    localtime_r(&t, &tm_time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_time);
    return std::string(buf);
}

/**
 * @brief 字符串转毫秒时间戳
 *
 * @param str 格式化时间字符串（如 "2024-07-28 10:00:00"）
 * @return int64_t 毫秒时间戳，解析失败返回 0
 */
inline int64_t string_to_ms(const std::string& str) {
    std::tm tm_time = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm_time, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    time_t t = mktime(&tm_time);
    return static_cast<int64_t>(t) * 1000;
}

} // namespace time_util

/**
 * @brief 消息存储服务 RPC 接口实现类
 *
 * 继承 MsgStorageService（由 message.proto 生成的服务基类），
 * 实现消息存储子服务的所有 RPC 接口和业务逻辑。
 *
 * 依赖组件：
 *   - MessageTable     : MySQL 数据库操作
 *   - MessageES        : Elasticsearch 文本搜索
 *   - ServiceChannelPool : RPC 服务发现与调用
 *   - MQClient         : RabbitMQ 消息消费
 */
class MsgStorageServiceImpl : public MsgStorageService {
public:
    MsgStorageServiceImpl();
    virtual ~MsgStorageServiceImpl();

    // ==================== 依赖注入方法 ====================

    /**
     * @brief 设置数据库操作组件
     * @param table MessageTable 共享指针
     */
    void set_message_table(std::shared_ptr<message_table::MessageTable> table);

    /**
     * @brief 设置 ES 客户端组件
     * @param es MessageES 共享指针
     */
    void set_message_es(std::shared_ptr<message_es::MessageES> es);

    /**
     * @brief 设置 RPC 信道池（用于调用其他微服务）
     * @param channel_pool ServiceChannelPool 共享指针
     */
    void set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool);

    /**
     * @brief 设置 MQ 客户端（用于消费消息和手动确认）
     * @param mq_client MQClient 共享指针
     */
    void set_mq_client(std::shared_ptr<mq::MQClient> mq_client);

    // ==================== RPC 接口实现 ====================

    /**
     * @brief 获取指定时间段的历史消息
     *
     * 流程：
     *   1. 从请求获取会话ID、起始时间、结束时间
     *   2. 调用 MessageTable::select_by_time_range 查询数据库
     *   3. 遍历结果，构造完整消息对象（获取发送者信息、文件数据）
     *   4. 返回消息列表
     *
     * @param cntl_base brpc 控制器
     * @param request   请求体（包含 chat_session_id, start_time, over_time）
     * @param response  响应体（包含 msg_list）
     * @param done      回调闭包
     */
    virtual void GetHistoryMsg(google::protobuf::RpcController* cntl_base,
                                const GetHistoryMsgReq* request,
                                GetHistoryMsgRsp* response,
                                google::protobuf::Closure* done);

    /**
     * @brief 获取最近N条消息
     *
     * 流程：
     *   1. 从请求获取会话ID、消息数量、可选的截止时间
     *   2. 有截止时间则调用 select_recent_before，否则调用 select_recent
     *   3. 遍历结果，构造完整消息对象
     *   4. 返回消息列表
     *
     * @param cntl_base brpc 控制器
     * @param request   请求体（包含 chat_session_id, msg_count, cur_time 可选）
     * @param response  响应体（包含 msg_list）
     * @param done      回调闭包
     */
    virtual void GetRecentMsg(google::protobuf::RpcController* cntl_base,
                               const GetRecentMsgReq* request,
                               GetRecentMsgRsp* response,
                               google::protobuf::Closure* done);

    /**
     * @brief 关键字消息搜索
     *
     * 流程：
     *   1. 从请求获取会话ID、搜索关键字
     *   2. 调用 ES 进行关键字搜索（会话过滤 + 内容匹配）
     *   3. 根据 ES 返回的消息ID列表，从 MySQL 回查完整消息信息
     *   4. 构造完整消息对象并返回
     *
     * @param cntl_base brpc 控制器
     * @param request   请求体（包含 chat_session_id, search_key）
     * @param response  响应体（包含 msg_list）
     * @param done      回调闭包
     */
    virtual void MsgSearch(google::protobuf::RpcController* cntl_base,
                            const MsgSearchReq* request,
                            MsgSearchRsp* response,
                            google::protobuf::Closure* done);

    // ==================== MQ 消息消费回调 ====================

    /**
     * @brief RabbitMQ 消息消费回调函数
     *
     * 处理流程：
     *   1. 解析 protobuf 消息
     *   2. 构造数据库记录并写入 MySQL
     *   3. 文本消息同时写入 Elasticsearch
     *   4. 文件/图片/语音消息转储到文件子服务
     *   5. 成功则 ack，失败则 reject
     *
     * @param message_str 消息序列化字符串
     * @param deliveryTag MQ 投递标签（用于 ack/reject）
     */
    void on_message_consume(const std::string& message_str, uint64_t deliveryTag);

private:
    // ==================== 私有辅助方法 ====================

    /**
     * @brief 调用用户子服务获取用户信息
     * @param user_id 用户ID
     * @param user_info 输出参数，获取到的用户信息
     * @return true 获取成功
     */
    bool get_user_info(const std::string& user_id, file::UserInfo& user_info);

    /**
     * @brief 调用文件子服务获取文件数据
     * @param file_id 文件ID
     * @param file_data 输出参数，获取到的文件下载数据
     * @return true 获取成功
     */
    bool get_file_data(const std::string& file_id, file::FileDownloadData& file_data);

    /**
     * @brief 从数据库记录构造完整的消息对象
     *
     * 根据消息类型，从数据库记录组装完整的 protobuf 消息：
     *   - 文本消息：直接读取 content 字段
     *   - 图片/语音消息：通过 file_id 从文件子服务获取数据
     *   - 文件消息：从数据库读取文件名、大小等元信息
     *   - 同时获取发送者用户信息
     *
     * @param msg_ptr 数据库消息记录智能指针
     * @return file::MessageInfo 完整的消息对象
     */
    file::MessageInfo construct_message_info(std::shared_ptr<msg_record> msg_ptr);

    /**
     * @brief 将文件消息转储到文件子服务
     *
     * 对于图片、文件、语音类型的消息，将文件二进制数据
     * 通过 RPC 上传到文件管理子服务进行持久化存储。
     * 如果消息已携带 file_id，则直接返回该 file_id。
     *
     * @param msg_info 包含文件数据的消息对象
     * @param out_file_id 输出参数：文件子服务返回的 file_id
     * @return true 存储成功
     */
    bool store_file_message(const file::MessageInfo& msg_info, std::string& out_file_id);

    /**
     * @brief 数据库消息类型转 protobuf 消息类型
     * @param db_type 数据库存储的类型值（0-3）
     * @return file::MessageType protobuf 枚举值
     */
    file::MessageType convert_message_type(signed char db_type);

    // ==================== 成员变量 ====================

    // MySQL 数据库操作组件
    std::shared_ptr<message_table::MessageTable> message_table_;
    // Elasticsearch 文本搜索组件
    std::shared_ptr<message_es::MessageES> message_es_;
    // RPC 信道池（用于调用用户/文件子服务）
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;
    // MQ 客户端（用于消息消费确认）
    std::shared_ptr<mq::MQClient> mq_client_;
};

} // namespace message