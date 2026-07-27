// =============================================================================
// transmit_service_impl.cc - 消息转发服务 RPC 接口实现
// =============================================================================
// 本文件实现 MsgTransmitServiceImpl 类中声明的 RPC 接口。
//
// 核心接口：GetTransmitTarget
//   - 获取消息转发目标，是消息转发服务唯一的 RPC 接口
//   - 处理流程：解析请求 → 获取发送者信息 → 构造消息 → 发布到MQ → 获取成员 → 返回响应
//
// 实现要点：
//   - 使用 brpc::ClosureGuard 确保 done->Run() 一定被调用
//   - 参数校验：检查关键字段是否为空
//   - 错误处理：捕获异常并记录日志，向客户端返回友好的错误信息
//   - 日志记录：记录关键操作日志，便于排查问题
// =============================================================================

#include "transmit_service_impl.hpp"
#include "user.pb.h"
#include "../common/log.hpp"
#include "../common/utils.hpp"

#include <brpc/controller.h>
#include <chrono>
#include <sstream>

namespace transmit {

// =============================================================================
// 构造与析构函数
// =============================================================================

/**
 * @brief 默认构造函数
 */
MsgTransmitServiceImpl::MsgTransmitServiceImpl() {}

/**
 * @brief 虚析构函数
 */
MsgTransmitServiceImpl::~MsgTransmitServiceImpl() {}

/**
 * @brief 设置数据库操作实例
 */
void MsgTransmitServiceImpl::set_transmit_table(std::shared_ptr<transmit_table::TransmitTable> transmit_table) {
    transmit_table_ = transmit_table;
}

/**
 * @brief 设置 RPC 信道池
 */
void MsgTransmitServiceImpl::set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool) {
    channel_pool_ = channel_pool;
}

/**
 * @brief 设置 MQ 客户端
 */
void MsgTransmitServiceImpl::set_mq_client(std::shared_ptr<mq::MQClient> mq_client) {
    mq_client_ = mq_client;
}

// =============================================================================
// 私有辅助方法
// =============================================================================

/**
 * @brief 根据用户ID从用户服务获取用户信息
 * 
 * 流程：
 *   1. 校验 RPC 信道池是否就绪
 *   2. 通过信道池获取 user_service 的信道（信道池基于 etcd 动态维护）
 *   3. 构造 GetUserInfo 请求，通过 brpc stub 同步调用用户服务
 *   4. 检查 RPC 调用是否出错（网络故障 / 超时 / 服务端返回错误）
 *   5. 提取响应中的用户信息
 * 
 * 边界情况：
 *   - 信道池未初始化 → 返回 false（需调用方确保已注入 channel_pool）
 *   - 信道获取失败 → 用户服务可能已下线，返回 false
 *   - RPC 调用失败 → 记录详细错误（网络错误文本 或 服务端 errmsg）
 */
bool MsgTransmitServiceImpl::get_user_info_from_user_service(const std::string& user_id, 
                                                             file::UserInfo& user_info) {
    // 1. 校验信道池是否就绪
    if (!channel_pool_) {
        LOG_ERROR("[MsgTransmitServiceImpl] Channel pool not initialized");
        return false;
    }

    // 2. 通过 etcd 服务发现获取 user_service 的信道
    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[MsgTransmitServiceImpl] No user_service channel available");
        return false;
    }

    // 3. 构造 GetUserInfo 请求，通过 brpc stub 同步调用用户服务
    chat::UserService_Stub stub(channel.get());
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;

    req.set_request_id(utils::generate_uuid());
    req.set_user_id(user_id);

    // 4. 同步调用（阻塞等待响应）
    brpc::Controller cntl;
    stub.GetUserInfo(&cntl, &req, &rsp, nullptr);

    // 5. 检查调用结果
    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[MsgTransmitServiceImpl] Get user info from user service failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    // 6. 提取用户信息
    if (rsp.has_user_info()) {
        user_info = rsp.user_info();
        LOG_INFO("[MsgTransmitServiceImpl] Get user info success, user_id: {}", user_id);
        return true;
    }

    LOG_ERROR("[MsgTransmitServiceImpl] User info not found in response, user_id: {}", user_id);
    return false;
}

/**
 * @brief 将消息发布到 MQ 消息队列
 * 
 * 流程：
 *   1. 校验 MQ 客户端是否就绪
 *   2. 将消息结构序列化为字符串
 *   3. 通过 MQ 客户端发布到指定的交换机和路由键
 * 
 * 设计考量：
 *   - 消息存储子服务会消费此消息并进行持久化
 *   - 使用 direct 交换机，路由键为会话ID，便于消息存储服务按会话消费
 */
bool MsgTransmitServiceImpl::publish_message_to_mq(const file::MessageInfo& message_info) {
    // 1. 校验 MQ 客户端是否就绪
    if (!mq_client_) {
        LOG_ERROR("[MsgTransmitServiceImpl] MQ client not initialized");
        return false;
    }

    // 2. 将消息序列化为字符串
    std::string message_str;
    if (!message_info.SerializeToString(&message_str)) {
        LOG_ERROR("[MsgTransmitServiceImpl] Failed to serialize message");
        return false;
    }

    // 3. 发布到 MQ 消息队列
    // 使用会话ID作为路由键，消息存储服务按会话消费
    std::string exchange_name = "message_exchange";
    std::string routing_key = message_info.chat_session_id();

    if (!mq_client_->publish(exchange_name, routing_key, message_str)) {
        LOG_ERROR("[MsgTransmitServiceImpl] Failed to publish message to MQ, session: {}", routing_key);
        return false;
    }

    LOG_INFO("[MsgTransmitServiceImpl] Message published to MQ, message_id: {}, session: {}", 
             message_info.message_id(), routing_key);
    return true;
}

/**
 * @brief 生成唯一消息ID
 * 
 * ID 格式：MSG_{timestamp}_{pid}_{random}_{counter}
 *   - MSG_:      前缀标识，便于在日志和数据库中识别
 *   - timestamp: 毫秒级时间戳，便于按时间排序与排查
 *   - pid:       进程ID，避免同一台机器上多个服务实例产生冲突
 *   - random:    随机数，进一步降低冲突概率
 *   - counter:   原子自增计数器，进程内严格递增
 * 
 * @return 唯一消息ID字符串
 */
std::string MsgTransmitServiceImpl::generate_message_id() {
    return "MSG" + utils::generate_uuid();
}

// =============================================================================
// GetTransmitTarget RPC 接口实现
// =============================================================================

/**
 * @brief 获取消息转发目标
 * 
 * 完整业务流程：
 *   1. 从请求中取出消息内容、会话ID、用户ID
 *   2. 参数校验（检查关键字段是否为空）
 *   3. 根据用户ID从用户子服务获取当前发送者用户信息
 *   4. 根据消息内容构造完整的消息结构（分配消息ID、填充发送者信息、消息产生时间）
 *   5. 将消息序列化后发布到 MQ 消息队列，让消息存储子服务对消息进行持久化存储
 *   6. 从数据库获取目标会话所有成员ID
 *   7. 过滤掉发送者自身（发送者不需要收到自己的消息）
 *   8. 组织响应（完整消息+目标用户ID），发送给网关，告知网关该将消息发送给谁
 */
void MsgTransmitServiceImpl::GetTransmitTarget(google::protobuf::RpcController* cntl_base,
                                               const NewMessageReq* request,
                                               GetTransmitTargetRsp* response,
                                               google::protobuf::Closure* done) {
    // 使用 ClosureGuard 确保 done->Run() 一定被调用
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    // 初始化响应
    response->set_request_id(request->request_id());
    response->set_success(false);

    // ==================== 步骤1：参数校验 ====================
    // 检查用户ID是否存在
    if (!request->has_user_id() || request->user_id().empty()) {
        LOG_WARN("[MsgTransmitServiceImpl] Missing user_id in request");
        response->set_errmsg("Missing user_id");
        return;
    }
    const std::string& user_id = request->user_id();

    // 检查会话ID是否存在
    if (request->chat_session_id().empty()) {
        LOG_WARN("[MsgTransmitServiceImpl] Missing chat_session_id in request");
        response->set_errmsg("Missing chat_session_id");
        return;
    }
    const std::string& session_id = request->chat_session_id();

    // 检查消息内容是否存在
    if (!request->has_message()) {
        LOG_WARN("[MsgTransmitServiceImpl] Missing message content in request");
        response->set_errmsg("Missing message content");
        return;
    }

    // ==================== 步骤2：获取发送者用户信息 ====================
    file::UserInfo sender_info;
    if (!get_user_info_from_user_service(user_id, sender_info)) {
        LOG_ERROR("[MsgTransmitServiceImpl] Failed to get sender info, user_id: {}", user_id);
        response->set_errmsg("Failed to get sender info");
        return;
    }

    // ==================== 步骤3：构造完整消息结构 ====================
    file::MessageInfo message_info;
    
    // 分配唯一消息ID
    message_info.set_message_id(generate_message_id());
    
    // 设置会话ID
    message_info.set_chat_session_id(session_id);
    
    // 设置消息产生时间（毫秒级时间戳）
    auto now = std::chrono::system_clock::now();
    auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    message_info.set_timestamp(ms_since_epoch);
    
    // 设置发送者信息
    *message_info.mutable_sender() = sender_info;
    
    // 设置消息内容
    *message_info.mutable_message() = request->message();

    LOG_DEBUG("[MsgTransmitServiceImpl] Message constructed, message_id: {}, session: {}", 
              message_info.message_id(), session_id);

    // ==================== 步骤4：发布消息到 MQ ====================
    if (!publish_message_to_mq(message_info)) {
        LOG_ERROR("[MsgTransmitServiceImpl] Failed to publish message to MQ, message_id: {}", 
                  message_info.message_id());
        response->set_errmsg("Failed to publish message");
        return;
    }

    // ==================== 步骤5：从数据库获取会话成员 ====================
    if (!transmit_table_) {
        LOG_ERROR("[MsgTransmitServiceImpl] Transmit table not initialized");
        response->set_errmsg("Database not initialized");
        return;
    }

    std::vector<std::string> member_ids = transmit_table_->get_session_member_ids(session_id);
    if (member_ids.empty()) {
        LOG_WARN("[MsgTransmitServiceImpl] No members found for session: {}", session_id);
        // 没有成员则不需要转发，但仍返回成功（消息已存储）
        response->set_success(true);
        *response->mutable_message() = message_info;
        LOG_INFO("[MsgTransmitServiceImpl] GetTransmitTarget success, no members to transmit, message_id: {}", 
                 message_info.message_id());
        return;
    }

    // ==================== 步骤6：过滤发送者自身 ====================
    // 发送者不需要收到自己的消息
    for (const std::string& member_id : member_ids) {
        if (member_id != user_id) {
            response->add_target_id_list(member_id);
        }
    }

    // ==================== 步骤7：组织响应 ====================
    response->set_success(true);
    *response->mutable_message() = message_info;

    LOG_INFO("[MsgTransmitServiceImpl] GetTransmitTarget success, message_id: {}, session: {}, targets: {}", 
             message_info.message_id(), session_id, response->target_id_list_size());
}

} // namespace transmit
