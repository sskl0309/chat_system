// =============================================================================
// transmit_service_impl.hpp - 消息转发服务 RPC 接口实现声明
// =============================================================================
// 本文件声明 MsgTransmitServiceImpl 类，继承自 protobuf 自动生成的 MsgTransmitService 基类，
// 实现消息转发服务的核心 RPC 接口。
//
// 核心功能：
//   1. GetTransmitTarget - 获取消息转发目标
//      - 从请求中提取消息内容、会话ID、用户ID
//      - 根据用户ID从用户子服务获取发送者信息
//      - 构造完整消息结构（分配消息ID、填充发送者信息、消息产生时间）
//      - 将消息序列化后发布到 MQ 消息队列
//      - 从数据库获取目标会话所有成员ID
//      - 组织响应（完整消息+目标用户ID）返回给网关
//
// 依赖组件：
//   - TransmitTable:    数据库操作（获取会话成员）
//   - ServiceChannelPool: RPC 信道池（调用用户服务）
//   - MQClient:         消息队列客户端（发布消息到 MQ）
// =============================================================================

#pragma once

#include <memory>
#include <string>

#include "transmit.pb.h"
#include "transmit_table.hpp"
#include "../common/brpc_client.hpp"
#include "../common/mq_client.hpp"

namespace transmit {

/**
 * @brief 消息转发服务 RPC 接口实现类
 * 
 * 继承自 protobuf 生成的 MsgTransmitService 基类，实现消息转发业务逻辑。
 * 通过依赖注入的方式接收各组件实例，实现业务与数据访问解耦。
 */
class MsgTransmitServiceImpl : public MsgTransmitService {
public:
    /**
     * @brief 默认构造函数
     * 
     * 创建空的服务实现对象，各组件需通过 set_xxx() 后续设置。
     */
    MsgTransmitServiceImpl();

    /**
     * @brief 虚析构函数
     */
    virtual ~MsgTransmitServiceImpl();

    // ==================== 依赖设置方法 ====================

    /**
     * @brief 设置数据库操作实例
     * 
     * @param transmit_table 消息转发服务数据库操作实例
     */
    void set_transmit_table(std::shared_ptr<transmit_table::TransmitTable> transmit_table);

    /**
     * @brief 设置 RPC 信道池
     * 
     * @param channel_pool brpc 服务信道池（用于调用用户服务）
     */
    void set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool);

    /**
     * @brief 设置 MQ 客户端
     * 
     * @param mq_client RabbitMQ 消息队列客户端（用于发布消息）
     */
    void set_mq_client(std::shared_ptr<mq::MQClient> mq_client);

public:
    // ==================== RPC 接口实现 ====================

    /**
     * @brief 获取消息转发目标
     * 
     * 核心业务流程：
     *   1. 从请求中取出消息内容、会话ID、用户ID
     *   2. 根据用户ID从用户子服务获取当前发送者用户信息
     *   3. 根据消息内容构造完整的消息结构（分配消息ID、填充发送者信息、消息产生时间）
     *   4. 将消息序列化后发布到 MQ 消息队列，让消息存储子服务对消息进行持久化存储
     *   5. 从数据库获取目标会话所有成员ID
     *   6. 组织响应（完整消息+目标用户ID），发送给网关，告知网关该将消息发送给谁
     * 
     * @param cntl_base RPC 控制器基类指针
     * @param request   请求消息
     * @param response  响应消息
     * @param done      完成回调
     */
    virtual void GetTransmitTarget(google::protobuf::RpcController* cntl_base,
                                   const NewMessageReq* request,
                                   GetTransmitTargetRsp* response,
                                   google::protobuf::Closure* done);

private:
    // ==================== 私有辅助方法 ====================

    /**
     * @brief 根据用户ID从用户服务获取用户信息
     * 
     * 通过 brpc 调用用户服务的 GetUserInfo 接口，获取发送者的详细信息。
     * 
     * @param user_id    用户唯一标识
     * @param user_info  输出参数，存储获取到的用户信息
     * @return 获取成功返回 true，失败返回 false
     */
    bool get_user_info_from_user_service(const std::string& user_id, 
                                         file::UserInfo& user_info);

    /**
     * @brief 将消息发布到 MQ 消息队列
     * 
     * 将完整的消息结构序列化后发布到 RabbitMQ，
     * 由消息存储子服务消费并进行持久化存储。
     * 
     * @param message_info 完整的消息信息
     * @return 发布成功返回 true，失败返回 false
     */
    bool publish_message_to_mq(const file::MessageInfo& message_info);

    /**
     * @brief 生成唯一消息ID
     * 
     * 使用时间戳+随机数+计数器的方式生成全局唯一的消息ID。
     * 
     * @return 唯一消息ID字符串
     */
    std::string generate_message_id();

    // ==================== 成员变量 ====================

    std::shared_ptr<transmit_table::TransmitTable> transmit_table_;  ///< 数据库操作实例
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;         ///< RPC 信道池
    std::shared_ptr<mq::MQClient> mq_client_;                       ///< MQ 消息队列客户端
};

} // namespace transmit
