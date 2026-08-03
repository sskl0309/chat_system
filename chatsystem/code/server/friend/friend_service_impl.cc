// =============================================================================
// friend_service_impl.cc - 好友管理服务 RPC 接口实现
// =============================================================================

#include "friend_service_impl.hpp"
#include "user.pb.h"
#include "file.pb.h"
#include "message.pb.h"
#include "../common/log.hpp"
#include "../common/utils.hpp"

#include <brpc/controller.h>
#include <brpc/closure_guard.h>

namespace friendsvc_ns = friendsvc;

namespace friend_service {

FriendServiceImpl::FriendServiceImpl() {}
FriendServiceImpl::~FriendServiceImpl() {}

void FriendServiceImpl::set_friend_table(std::shared_ptr<friend_table::FriendTable> table) {
    friend_table_ = table;
}

void FriendServiceImpl::set_friend_es(std::shared_ptr<friend_es::FriendES> es) {
    friend_es_ = es;
}

void FriendServiceImpl::set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool) {
    channel_pool_ = channel_pool;
}

// ==================== 私有辅助方法 ====================

bool FriendServiceImpl::get_user_info(const std::string& user_id, file::UserInfo& user_info) {
    if (!channel_pool_) {
        LOG_ERROR("[FriendServiceImpl] Channel pool not initialized");
        return false;
    }
    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[FriendServiceImpl] No user_service channel");
        return false;
    }
    chat::UserService_Stub stub(channel.get());
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;
    req.set_request_id(utils::generate_uuid());
    req.set_user_id(user_id);
    brpc::Controller cntl;
    stub.GetUserInfo(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[FriendServiceImpl] GetUserInfo failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }
    user_info = rsp.user_info();
    return !user_info.user_id().empty();
}

bool FriendServiceImpl::get_multi_user_info(const std::vector<std::string>& user_ids,
                                            std::map<std::string, file::UserInfo>& user_map) {
    if (!channel_pool_ || user_ids.empty()) return false;
    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[FriendServiceImpl] No user_service channel");
        return false;
    }
    chat::UserService_Stub stub(channel.get());
    chat::GetMultiUserInfoReq req;
    chat::GetMultiUserInfoRsp rsp;
    req.set_request_id(utils::generate_uuid());
    for (const auto& uid : user_ids) {
        req.add_users_id(uid);
    }
    brpc::Controller cntl;
    stub.GetMultiUserInfo(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[FriendServiceImpl] GetMultiUserInfo failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }
    const auto& users_info = rsp.users_info();
    for (auto it = users_info.begin(); it != users_info.end(); ++it) {
        user_map[it->first] = it->second;
    }
    return true;
}

bool FriendServiceImpl::get_avatar_data(const std::string& avatar_id, std::string& avatar_data) {
    if (!channel_pool_ || avatar_id.empty()) return false;
    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) return false;
    file::FileService_Stub stub(channel.get());
    file::GetSingleFileReq req;
    file::GetSingleFileRsp rsp;
    req.set_request_id(utils::generate_uuid());
    req.set_file_id(avatar_id);
    brpc::Controller cntl;
    stub.GetSingleFile(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed() || !rsp.success()) return false;
    avatar_data = rsp.file_data().file_content();
    return !avatar_data.empty();
}

bool FriendServiceImpl::get_recent_msg(const std::string& session_id, file::MessageInfo& msg_info) {
    if (!channel_pool_ || session_id.empty()) return false;
    auto channel = channel_pool_->get_channel("message_storage_service");
    if (!channel) return false;
    message::MsgStorageService_Stub stub(channel.get());
    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    req.set_request_id(utils::generate_uuid());
    req.set_chat_session_id(session_id);
    req.set_msg_count(1);
    brpc::Controller cntl;
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed() || !rsp.success()) return false;
    if (rsp.msg_list_size() > 0) {
        msg_info = rsp.msg_list(0);
        return true;
    }
    return false;
}

bool FriendServiceImpl::fill_user_info_avatar(file::UserInfo& user_info) {
    if (user_info.avatar().empty()) {
        file::UserInfo full_info;
        if (get_user_info(user_info.user_id(), full_info)) {
            if (!full_info.avatar().empty()) {
                user_info.set_avatar(full_info.avatar());
                return true;
            }
        }
    }
    return false;
}

// ==================== GetFriendList ====================
void FriendServiceImpl::GetFriendList(google::protobuf::RpcController* cntl_base,
                                       const friendsvc_ns::GetFriendListReq* request,
                                       friendsvc_ns::GetFriendListRsp* response,
                                       google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& user_id = request->user_id();
    // 获取所有好友ID
    std::vector<std::string> friend_ids = friend_table_->get_friend_ids(user_id);

    // 批量获取好友信息
    std::map<std::string, file::UserInfo> user_map;
    if (!friend_ids.empty()) {
        get_multi_user_info(friend_ids, user_map);
    }

    // 组装响应
    auto* list = response->mutable_friend_list();
    for (const auto& fid : friend_ids) {
        auto* info = list->Add();
        info->set_user_id(fid);
        auto it = user_map.find(fid);
        if (it != user_map.end()) {
            *info = it->second;
            // 补充头像（如未获取到）
            if (info->avatar().empty()) {
                fill_user_info_avatar(*info);
            }
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] GetFriendList: user={}, friends={}",
             user_id, friend_ids.size());
}

// ==================== FriendAdd ====================
void FriendServiceImpl::FriendAdd(google::protobuf::RpcController* cntl_base,
                                   const friendsvc_ns::FriendAddReq* request,
                                   friendsvc_ns::FriendAddRsp* response,
                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty() ||
        request->respondent_id().empty()) {
        response->set_errmsg("Invalid request: missing user_id or respondent_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& req_id = request->user_id();
    const std::string& rsp_id = request->respondent_id();

    if (req_id == rsp_id) {
        response->set_errmsg("Cannot add yourself as friend");
        return;
    }

    // 1. 判断是否已是好友
    if (friend_table_->is_friend(req_id, rsp_id)) {
        response->set_errmsg("Already friends");
        return;
    }

    // 2. 判断是否已申请过
    if (friend_table_->has_pending_event(req_id, rsp_id)) {
        response->set_errmsg("Already applied, please wait");
        return;
    }

    // 3. 创建好友申请事件
    std::string event_id = "FEVENT_" + utils::generate_uuid();
    if (!friend_table_->add_event(event_id, req_id, rsp_id)) {
        response->set_errmsg("Failed to create friend event");
        return;
    }

    response->set_notify_event_id(event_id);
    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] FriendAdd: {} -> {}, event={}", req_id, rsp_id, event_id);
}

// ==================== GetPendingFriendEventList ====================
void FriendServiceImpl::GetPendingFriendEventList(google::protobuf::RpcController* cntl_base,
                                                   const friendsvc_ns::GetPendingFriendEventListReq* request,
                                                   friendsvc_ns::GetPendingFriendEventListRsp* response,
                                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& user_id = request->user_id();
    auto events = friend_table_->get_pending_events(user_id);

    // 收集所有申请者ID
    std::vector<std::string> applicant_ids;
    std::map<std::string, std::string> event_user_map;  // event_id -> applicant_id
    for (const auto& ev : events) {
        applicant_ids.push_back(ev->req_user_id());
        event_user_map[ev->event_id()] = ev->req_user_id();
    }

    // 批量获取申请者信息
    std::map<std::string, file::UserInfo> user_map;
    if (!applicant_ids.empty()) {
        get_multi_user_info(applicant_ids, user_map);
    }

    // 组装响应
    auto* list = response->mutable_event();
    for (const auto& ev : events) {
        auto* fe = list->Add();
        fe->set_event_id(ev->event_id());
        auto it = user_map.find(ev->req_user_id());
        if (it != user_map.end()) {
            *fe->mutable_sender() = it->second;
            if (fe->sender().avatar().empty()) {
                fill_user_info_avatar(*fe->mutable_sender());
            }
        } else {
            fe->mutable_sender()->set_user_id(ev->req_user_id());
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] GetPendingFriendEventList: user={}, events={}",
             user_id, events.size());
}

// ==================== FriendAddProcess ====================
void FriendServiceImpl::FriendAddProcess(google::protobuf::RpcController* cntl_base,
                                          const friendsvc_ns::FriendAddProcessReq* request,
                                          friendsvc_ns::FriendAddProcessRsp* response,
                                          google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (request->notify_event_id().empty()) {
        response->set_errmsg("Missing notify_event_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& event_id = request->notify_event_id();
    auto event = friend_table_->get_event_by_id(event_id);
    if (!event) {
        response->set_errmsg("Friend event not found");
        return;
    }

    const std::string& req_user_id = event->req_user_id();
    const std::string& rsp_user_id = event->rsp_user_id();

    // 判断是否已是好友
    if (friend_table_->is_friend(req_user_id, rsp_user_id)) {
        response->set_errmsg("Already friends");
        // 仍删除事件
        friend_table_->remove_event(event_id);
        return;
    }

    // 先删除事件
    friend_table_->remove_event(event_id);

    if (request->agree()) {
        // 1. 添加好友关系
        if (!friend_table_->add_friend_relation(req_user_id, rsp_user_id)) {
            response->set_errmsg("Failed to add friend relation");
            return;
        }

        // 2. 创建单聊会话
        std::string session_id = "SESS_" + utils::generate_uuid();
        if (!friend_table_->add_session(session_id, "", session_type_t::SINGLE)) {
            response->set_errmsg("Failed to create session");
            return;
        }

        // 3. 添加会话成员
        std::vector<std::string> members = {req_user_id, rsp_user_id};
        if (!friend_table_->add_session_members(session_id, members)) {
            response->set_errmsg("Failed to add session members");
            return;
        }

        response->set_new_session_id(session_id);
        LOG_INFO("[FriendServiceImpl] FriendAddProcess accept: {} <-> {}, session={}",
                 req_user_id, rsp_user_id, session_id);
    } else {
        LOG_INFO("[FriendServiceImpl] FriendAddProcess reject: {} -> {}", req_user_id, rsp_user_id);
    }

    response->set_success(true);
}

// ==================== FriendRemove ====================
void FriendServiceImpl::FriendRemove(google::protobuf::RpcController* cntl_base,
                                      const friendsvc_ns::FriendRemoveReq* request,
                                      friendsvc_ns::FriendRemoveRsp* response,
                                      google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty() || request->peer_id().empty()) {
        response->set_errmsg("Invalid request");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& user_id = request->user_id();
    const std::string& peer_id = request->peer_id();

    // 1. 删除好友关系
    friend_table_->remove_friend_relation(user_id, peer_id);

    // 2. 查找两人之间的单聊会话并删除
    auto user_sessions = friend_table_->get_session_ids_by_user(user_id);
    auto peer_sessions = friend_table_->get_session_ids_by_user(peer_id);

    // 求交集找到两人共同的会话
    for (const auto& sid : user_sessions) {
        bool is_common = false;
        for (const auto& psid : peer_sessions) {
            if (sid == psid) { is_common = true; break; }
        }
        if (!is_common) continue;

        auto session = friend_table_->get_session_by_id(sid);
        if (!session) continue;

        // 找到会话所有成员，若恰好2人且为双方，则为单聊会话
        auto members = friend_table_->get_session_member_ids(sid);
        if (members.size() == 2) {
            bool contains_both = false;
            bool has_user = false, has_peer = false;
            for (const auto& m : members) {
                if (m == user_id) has_user = true;
                if (m == peer_id) has_peer = true;
            }
            contains_both = has_user && has_peer;
            if (contains_both && session->session_type() == session_type_t::SINGLE) {
                // 删除会话成员
                friend_table_->remove_all_session_members(sid);
                // 删除会话
                friend_table_->remove_session(sid);
                LOG_INFO("[FriendServiceImpl] FriendRemove removed session: {}", sid);
            }
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] FriendRemove: {} <-> {}", user_id, peer_id);
}

// ==================== FriendSearch ====================
void FriendServiceImpl::FriendSearch(google::protobuf::RpcController* cntl_base,
                                     const friendsvc_ns::FriendSearchReq* request,
                                     friendsvc_ns::FriendSearchRsp* response,
                                     google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    if (!friend_es_) {
        response->set_errmsg("ES not initialized");
        return;
    }

    const std::string& user_id = request->user_id();
    const std::string& keyword = request->search_key();

    // 收集需要排除的用户：自己 + 所有好友
    std::vector<std::string> exclude_uids;
    exclude_uids.push_back(user_id);
    auto friend_ids = friend_table_ ? friend_table_->get_friend_ids(user_id)
                                    : std::vector<std::string>();
    for (const auto& fid : friend_ids) {
        exclude_uids.push_back(fid);
    }

    // 调用ES搜索
    std::vector<friend_es::ESUser> es_users;
    if (!friend_es_->search_user(keyword, exclude_uids, es_users)) {
        response->set_errmsg("Search failed");
        return;
    }

    // 组装用户信息（带头像）
    auto* list = response->mutable_user_info();
    std::vector<std::string> ids_to_fetch_avatar;
    for (const auto& u : es_users) {
        auto* info = list->Add();
        info->set_user_id(u.user_id);
        info->set_nickname(u.nickname);
        info->set_email(u.email);
        info->set_description(u.description);
        if (!u.avatar_id.empty()) {
            std::string avatar_data;
            if (get_avatar_data(u.avatar_id, avatar_data)) {
                info->set_avatar(avatar_data);
            }
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] FriendSearch: keyword={}, results={}", keyword, list->size());
}

// ==================== ChatSessionCreate ====================
void FriendServiceImpl::ChatSessionCreate(google::protobuf::RpcController* cntl_base,
                                           const friendsvc_ns::ChatSessionCreateReq* request,
                                           friendsvc_ns::ChatSessionCreateRsp* response,
                                           google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& session_name = request->chat_session_name();
    std::vector<std::string> members;
    for (int i = 0; i < request->member_id_list_size(); ++i) {
        members.push_back(request->member_id_list(i));
    }

    if (members.empty()) {
        response->set_errmsg("Empty member list");
        return;
    }

    // 生成会话ID
    std::string session_id = "SESS_" + utils::generate_uuid();

    // 新增群聊会话
    if (!friend_table_->add_session(session_id, session_name, session_type_t::GROUP)) {
        response->set_errmsg("Failed to create session");
        return;
    }

    // 添加所有成员
    if (!friend_table_->add_session_members(session_id, members)) {
        response->set_errmsg("Failed to add members");
        return;
    }

    // 组装响应会话信息
    auto* session_info = response->mutable_chat_session_info();
    session_info->set_chat_session_id(session_id);
    session_info->set_chat_session_name(session_name);

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] ChatSessionCreate: id={}, name={}, members={}",
             session_id, session_name, members.size());
}

// ==================== GetChatSessionMember ====================
void FriendServiceImpl::GetChatSessionMember(google::protobuf::RpcController* cntl_base,
                                              const friendsvc_ns::GetChatSessionMemberReq* request,
                                              friendsvc_ns::GetChatSessionMemberRsp* response,
                                              google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (request->chat_session_id().empty()) {
        response->set_errmsg("Missing chat_session_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& session_id = request->chat_session_id();
    auto member_ids = friend_table_->get_session_member_ids(session_id);

    // 批量获取成员信息
    std::map<std::string, file::UserInfo> user_map;
    if (!member_ids.empty()) {
        get_multi_user_info(member_ids, user_map);
    }

    auto* list = response->mutable_member_info_list();
    for (const auto& mid : member_ids) {
        auto* info = list->Add();
        info->set_user_id(mid);
        auto it = user_map.find(mid);
        if (it != user_map.end()) {
            *info = it->second;
            if (info->avatar().empty()) {
                fill_user_info_avatar(*info);
            }
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] GetChatSessionMember: session={}, members={}",
             session_id, member_ids.size());
}

// ==================== GetChatSessionList ====================
void FriendServiceImpl::GetChatSessionList(google::protobuf::RpcController* cntl_base,
                                             const friendsvc_ns::GetChatSessionListReq* request,
                                             friendsvc_ns::GetChatSessionListRsp* response,
                                             google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& user_id = request->user_id();
    auto session_ids = friend_table_->get_session_ids_by_user(user_id);

    auto* list = response->mutable_chat_session_info_list();

    // 逐个会话处理
    for (const auto& sid : session_ids) {
        auto session = friend_table_->get_session_by_id(sid);
        if (!session) continue;

        auto* info = list->Add();
        info->set_chat_session_id(sid);

        auto member_ids = friend_table_->get_session_member_ids(sid);

        if (session->session_type() == session_type_t::SINGLE) {
            // 单聊会话：找到对方用户
            std::string peer_id;
            for (const auto& mid : member_ids) {
                if (mid != user_id) { peer_id = mid; break; }
            }
            info->set_single_chat_friend_id(peer_id);

            // 获取对方信息用于显示（会话名+头像）
            file::UserInfo peer_info;
            if (!peer_id.empty() && get_user_info(peer_id, peer_info)) {
                info->set_chat_session_name(peer_info.nickname());
                if (!peer_info.avatar().empty()) {
                    info->set_avatar(peer_info.avatar());
                } else {
                    std::string avatar_id;
                    // 尝试从用户信息中拿头像
                    file::UserInfo full_peer;
                    if (get_user_info(peer_id, full_peer) && !full_peer.avatar().empty()) {
                        info->set_avatar(full_peer.avatar());
                    }
                }
            } else {
                info->set_chat_session_name(peer_id);
            }
        } else {
            // 群聊会话
            std::string name = session->session_name().null() ? "" : session->session_name().get();
            info->set_chat_session_name(name.empty() ? "群聊" : name);
        }

        // 获取会话最新一条消息
        file::MessageInfo last_msg;
        if (get_recent_msg(sid, last_msg)) {
            *info->mutable_prev_message() = last_msg;
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] GetChatSessionList: user={}, sessions={}",
             user_id, list->size());
}

} // namespace friend_service
