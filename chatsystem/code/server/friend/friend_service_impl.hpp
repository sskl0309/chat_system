// =============================================================================
// friend_service_impl.hpp - 好友管理服务 RPC 接口实现声明
// =============================================================================
// 本头文件声明 FriendServiceImpl 类，继承自 protobuf 生成的 FriendService 基类，
// 实现好友管理子服务的核心 RPC 接口。
//
// 依赖组件：
//   - FriendTable         : MySQL 数据库操作（好友关系/事件/会话/会话成员）
//   - FriendES            : Elasticsearch 用户搜索
//   - ServiceChannelPool  : RPC 服务发现与调用（调用用户服务、消息服务、文件服务）
//
// 注意：
//   protobuf 中 package 名为 friendsvc，因 friend 是 C++ 关键字，
//   使用命名空间别名 friendsvc_ns 引用生成的类型。
// =============================================================================

#ifndef FRIEND_SERVICE_IMPL_HPP
#define FRIEND_SERVICE_IMPL_HPP

#include <memory>
#include <string>
#include <vector>
#include <map>

#include "friend.pb.h"
#include "friend_table.hpp"
#include "friend_es.hpp"
#include "../common/brpc_client.hpp"

// 因为 friend 是 C++ 关键字，使用命名空间别名引用 protobuf 生成的命名空间
namespace friendsvc_ns = friendsvc;

namespace friend_service {

/**
 * @brief 好友管理服务 RPC 接口实现类
 *
 * 继承自 protobuf 生成的 FriendService 基类，实现以下 9 个 RPC 方法：
 *   - GetFriendList            : 获取好友列表
 *   - FriendRemove             : 删除好友
 *   - FriendAdd                : 发送好友申请
 *   - FriendAddProcess         : 处理好友申请（同意/拒绝）
 *   - FriendSearch             : 搜索用户
 *   - GetChatSessionList       : 获取聊天会话列表
 *   - ChatSessionCreate        : 创建群聊会话
 *   - GetChatSessionMember     : 获取会话成员列表
 *   - GetPendingFriendEventList: 获取待处理好友申请列表
 *
 * 同时提供多个私有辅助方法，用于通过 RPC 调用其他微服务获取用户信息、头像、消息等。
 */
class FriendServiceImpl : public friendsvc_ns::FriendService {
public:
    FriendServiceImpl();
    virtual ~FriendServiceImpl();

    // ==================== 依赖注入接口 ====================

    /// 注入数据库操作对象
    void set_friend_table(std::shared_ptr<friend_table::FriendTable> table);
    /// 注入 Elasticsearch 搜索客户端
    void set_friend_es(std::shared_ptr<friend_es::FriendES> es);
    /// 注入 RPC 通道池（用于调用用户服务、文件服务、消息存储服务）
    void set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool);

    // ==================== RPC 接口实现 ====================

    /// 获取指定用户的好友列表（含好友详细信息与头像）
    virtual void GetFriendList(google::protobuf::RpcController* cntl_base,
                               const friendsvc_ns::GetFriendListReq* request,
                               friendsvc_ns::GetFriendListRsp* response,
                               google::protobuf::Closure* done);

    /// 删除好友关系，同时删除两人之间的单聊会话
    virtual void FriendRemove(google::protobuf::RpcController* cntl_base,
                             const friendsvc_ns::FriendRemoveReq* request,
                             friendsvc_ns::FriendRemoveRsp* response,
                             google::protobuf::Closure* done);

    /// 发送好友申请，创建待处理的好友申请事件
    virtual void FriendAdd(google::protobuf::RpcController* cntl_base,
                          const friendsvc_ns::FriendAddReq* request,
                          friendsvc_ns::FriendAddRsp* response,
                          google::protobuf::Closure* done);

    /// 处理好友申请：同意则建立好友关系并创建单聊会话，拒绝则仅删除事件
    virtual void FriendAddProcess(google::protobuf::RpcController* cntl_base,
                                 const friendsvc_ns::FriendAddProcessReq* request,
                                 friendsvc_ns::FriendAddProcessRsp* response,
                                 google::protobuf::Closure* done);

    /// 根据关键字搜索用户（排除自己和已有好友）
    virtual void FriendSearch(google::protobuf::RpcController* cntl_base,
                              const friendsvc_ns::FriendSearchReq* request,
                              friendsvc_ns::FriendSearchRsp* response,
                              google::protobuf::Closure* done);

    /// 获取指定用户参与的所有聊天会话列表（含会话最新消息）
    virtual void GetChatSessionList(google::protobuf::RpcController* cntl_base,
                                    const friendsvc_ns::GetChatSessionListReq* request,
                                    friendsvc_ns::GetChatSessionListRsp* response,
                                    google::protobuf::Closure* done);

    /// 创建群聊会话，将指定成员列表加入会话
    virtual void ChatSessionCreate(google::protobuf::RpcController* cntl_base,
                                   const friendsvc_ns::ChatSessionCreateReq* request,
                                   friendsvc_ns::ChatSessionCreateRsp* response,
                                   google::protobuf::Closure* done);

    /// 获取指定会话的成员列表（含成员详细信息与头像）
    virtual void GetChatSessionMember(google::protobuf::RpcController* cntl_base,
                                     const friendsvc_ns::GetChatSessionMemberReq* request,
                                     friendsvc_ns::GetChatSessionMemberRsp* response,
                                     google::protobuf::Closure* done);

    /// 获取待处理的好友申请列表（即"谁申请加我为好友"）
    virtual void GetPendingFriendEventList(google::protobuf::RpcController* cntl_base,
                                          const friendsvc_ns::GetPendingFriendEventListReq* request,
                                          friendsvc_ns::GetPendingFriendEventListRsp* response,
                                          google::protobuf::Closure* done);

private:
    // ==================== 私有辅助方法 ====================

    /**
     * @brief 通过 RPC 调用用户服务获取单个用户信息
     * @param user_id   用户ID
     * @param user_info 输出参数，接收用户信息
     * @return true 表示获取成功
     */
    bool get_user_info(const std::string& user_id, file::UserInfo& user_info);

    /**
     * @brief 通过 RPC 调用用户服务批量获取用户信息
     * @param user_ids  用户ID列表
     * @param user_map  输出参数，用户ID到用户信息的映射
     * @return true 表示获取成功
     */
    bool get_multi_user_info(const std::vector<std::string>& user_ids,
                             std::map<std::string, file::UserInfo>& user_map);

    /**
     * @brief 通过 RPC 调用文件服务获取头像数据
     * @param avatar_id   头像文件ID
     * @param avatar_data 输出参数，接收头像二进制数据
     * @return true 表示获取成功
     */
    bool get_avatar_data(const std::string& avatar_id, std::string& avatar_data);

    /**
     * @brief 通过 RPC 调用消息存储服务获取会话最新消息
     * @param session_id 会话ID
     * @param msg_info   输出参数，接收最新消息信息
     * @return true 表示获取成功（会话有消息时）
     */
    bool get_recent_msg(const std::string& session_id, file::MessageInfo& msg_info);

    /**
     * @brief 补充用户头像数据
     *
     * 当用户信息中头像为空时，通过 RPC 再次获取完整用户信息来补充头像。
     *
     * @param user_info 用户信息（输入输出参数）
     * @return true 表示头像补充成功
     */
    bool fill_user_info_avatar(file::UserInfo& user_info);

    // ==================== 成员变量 ====================

    std::shared_ptr<friend_table::FriendTable> friend_table_;    ///< 数据库操作对象
    std::shared_ptr<friend_es::FriendES> friend_es_;             ///< ES 搜索客户端
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;     ///< RPC 通道池
};

} // namespace friend_service

#endif
