// =============================================================================
// message_service_impl.hpp - 消息存储服务 RPC 接口实现声明
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
 * @brief 时间工具函数
 *
 * 处理毫秒时间戳与字符串格式的互转
 */
namespace time_util {

// 毫秒时间戳 -> "YYYY-MM-DD HH:MM:SS" 字符串
inline std::string ms_to_string(int64_t ms) {
    time_t seconds = ms / 1000;
    std::time_t t = seconds;
    std::tm tm_time;
    localtime_r(&t, &tm_time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_time);
    return std::string(buf);
}

// "YYYY-MM-DD HH:MM:SS" 字符串 -> 毫秒时间戳
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
 */
class MsgStorageServiceImpl : public MsgStorageService {
public:
    MsgStorageServiceImpl();
    virtual ~MsgStorageServiceImpl();

    void set_message_table(std::shared_ptr<message_table::MessageTable> table);
    void set_message_es(std::shared_ptr<message_es::MessageES> es);
    void set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool);
    void set_mq_client(std::shared_ptr<mq::MQClient> mq_client);

public:
    virtual void GetHistoryMsg(google::protobuf::RpcController* cntl_base,
                                const GetHistoryMsgReq* request,
                                GetHistoryMsgRsp* response,
                                google::protobuf::Closure* done);

    virtual void GetRecentMsg(google::protobuf::RpcController* cntl_base,
                               const GetRecentMsgReq* request,
                               GetRecentMsgRsp* response,
                               google::protobuf::Closure* done);

    virtual void MsgSearch(google::protobuf::RpcController* cntl_base,
                            const MsgSearchReq* request,
                            MsgSearchRsp* response,
                            google::protobuf::Closure* done);

    void on_message_consume(const std::string& message_str, uint64_t deliveryTag);

private:
    bool get_user_info(const std::string& user_id, file::UserInfo& user_info);
    bool get_file_data(const std::string& file_id, file::FileDownloadData& file_data);
    file::MessageInfo construct_message_info(std::shared_ptr<msg_record> msg_ptr);
    bool store_file_message(const file::MessageInfo& msg_info);
    file::MessageType convert_message_type(signed char db_type);

    std::shared_ptr<message_table::MessageTable> message_table_;
    std::shared_ptr<message_es::MessageES> message_es_;
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;
    std::shared_ptr<mq::MQClient> mq_client_;
};

} // namespace message