// =============================================================================
// friend_service_impl.hpp - 好友管理服务 RPC 接口实现声明
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

// 因为 friend 是 C++ 关键字，使用命名空间别名
namespace friendsvc_ns = friendsvc;

namespace friend_service {

class FriendServiceImpl : public friendsvc_ns::FriendService {
public:
    FriendServiceImpl();
    virtual ~FriendServiceImpl();

    void set_friend_table(std::shared_ptr<friend_table::FriendTable> table);
    void set_friend_es(std::shared_ptr<friend_es::FriendES> es);
    void set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool);

    virtual void GetFriendList(google::protobuf::RpcController* cntl_base,
                               const friendsvc_ns::GetFriendListReq* request,
                               friendsvc_ns::GetFriendListRsp* response,
                               google::protobuf::Closure* done);

    virtual void FriendRemove(google::protobuf::RpcController* cntl_base,
                             const friendsvc_ns::FriendRemoveReq* request,
                             friendsvc_ns::FriendRemoveRsp* response,
                             google::protobuf::Closure* done);

    virtual void FriendAdd(google::protobuf::RpcController* cntl_base,
                          const friendsvc_ns::FriendAddReq* request,
                          friendsvc_ns::FriendAddRsp* response,
                          google::protobuf::Closure* done);

    virtual void FriendAddProcess(google::protobuf::RpcController* cntl_base,
                                 const friendsvc_ns::FriendAddProcessReq* request,
                                 friendsvc_ns::FriendAddProcessRsp* response,
                                 google::protobuf::Closure* done);

    virtual void FriendSearch(google::protobuf::RpcController* cntl_base,
                              const friendsvc_ns::FriendSearchReq* request,
                              friendsvc_ns::FriendSearchRsp* response,
                              google::protobuf::Closure* done);

    virtual void GetChatSessionList(google::protobuf::RpcController* cntl_base,
                                    const friendsvc_ns::GetChatSessionListReq* request,
                                    friendsvc_ns::GetChatSessionListRsp* response,
                                    google::protobuf::Closure* done);

    virtual void ChatSessionCreate(google::protobuf::RpcController* cntl_base,
                                   const friendsvc_ns::ChatSessionCreateReq* request,
                                   friendsvc_ns::ChatSessionCreateRsp* response,
                                   google::protobuf::Closure* done);

    virtual void GetChatSessionMember(google::protobuf::RpcController* cntl_base,
                                     const friendsvc_ns::GetChatSessionMemberReq* request,
                                     friendsvc_ns::GetChatSessionMemberRsp* response,
                                     google::protobuf::Closure* done);

    virtual void GetPendingFriendEventList(google::protobuf::RpcController* cntl_base,
                                          const friendsvc_ns::GetPendingFriendEventListReq* request,
                                          friendsvc_ns::GetPendingFriendEventListRsp* response,
                                          google::protobuf::Closure* done);

private:
    bool get_user_info(const std::string& user_id, file::UserInfo& user_info);
    bool get_multi_user_info(const std::vector<std::string>& user_ids,
                             std::map<std::string, file::UserInfo>& user_map);
    bool get_avatar_data(const std::string& avatar_id, std::string& avatar_data);
    bool get_recent_msg(const std::string& session_id, file::MessageInfo& msg_info);
    bool fill_user_info_avatar(file::UserInfo& user_info);

    std::shared_ptr<friend_table::FriendTable> friend_table_;
    std::shared_ptr<friend_es::FriendES> friend_es_;
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;
};

} // namespace friend_service

#endif
