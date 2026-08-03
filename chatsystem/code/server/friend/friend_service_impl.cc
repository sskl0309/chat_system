// =============================================================================
// friend_service_impl.cc - 好友管理服务 RPC 接口实现
// =============================================================================
// 本文件实现 FriendServiceImpl 类的所有 RPC 方法，包括：
//   - 好友列表获取、好友删除、好友申请、好友申请处理
//   - 用户搜索、聊天会话列表、群聊创建、会话成员获取
//   - 待处理好友申请列表获取
//
// 实现要点：
//   1. 所有 RPC 方法使用 brpc::ClosureGuard 保证 done 回调被调用
//   2. 用户信息通过 RPC 调用用户服务 (user_service) 获取
//   3. 头像数据通过 RPC 调用文件服务 (file_service) 获取
//   4. 会话最新消息通过 RPC 调用消息存储服务 (message_storage_service) 获取
//   5. 用户搜索通过 Elasticsearch 实现
// =============================================================================

#include "friend_service_impl.hpp"
#include "user.pb.h"
#include "file.pb.h"
#include "message.pb.h"
#include "../common/log.hpp"
#include "../common/utils.hpp"

#include <brpc/controller.h>
#include <brpc/closure_guard.h>

// 命名空间别名：因 friend 是 C++ 关键字，使用 friendsvc_ns 引用 protobuf 生成的类型
namespace friendsvc_ns = friendsvc;

namespace friend_service {

// ==================== 构造/析构 ====================

FriendServiceImpl::FriendServiceImpl() {}
FriendServiceImpl::~FriendServiceImpl() {}

// ==================== 依赖注入实现 ====================

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

/**
 * @brief 通过 RPC 调用用户服务获取单个用户的详细信息
 *
 * 调用 user_service 的 GetUserInfo 接口，获取包含昵称、签名、邮箱、头像等信息。
 *
 * @param user_id   要查询的用户ID
 * @param user_info 输出参数，接收用户信息
 * @return true 表示成功获取且用户ID非空
 */
bool FriendServiceImpl::get_user_info(const std::string& user_id, file::UserInfo& user_info) {
    if (!channel_pool_) {
        LOG_ERROR("[FriendServiceImpl] Channel pool not initialized");
        return false;
    }
    // 从通道池获取用户服务的 RPC 通道
    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[FriendServiceImpl] No user_service channel");
        return false;
    }
    // 构造 RPC 请求
    chat::UserService_Stub stub(channel.get());
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;
    req.set_request_id(utils::generate_uuid());
    req.set_user_id(user_id);
    brpc::Controller cntl;
    stub.GetUserInfo(&cntl, &req, &rsp, nullptr);
    // 检查 RPC 调用结果
    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[FriendServiceImpl] GetUserInfo failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }
    user_info = rsp.user_info();
    return !user_info.user_id().empty();
}

/**
 * @brief 通过 RPC 调用用户服务批量获取用户信息
 *
 * 调用 user_service 的 GetMultiUserInfo 接口，一次性获取多个用户的信息，
 * 减少多次单独 RPC 调用的网络开销。
 *
 * @param user_ids  要查询的用户ID列表
 * @param user_map  输出参数，用户ID到用户信息的映射
 * @return true 表示调用成功
 */
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
    // 填充所有待查询的用户ID
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
    // 将返回的映射表复制到输出参数
    const auto& users_info = rsp.users_info();
    for (auto it = users_info.begin(); it != users_info.end(); ++it) {
        user_map[it->first] = it->second;
    }
    return true;
}

/**
 * @brief 通过 RPC 调用文件服务获取头像数据
 *
 * 调用 file_service 的 GetSingleFile 接口，根据文件ID下载头像的二进制数据。
 *
 * @param avatar_id   头像文件ID
 * @param avatar_data 输出参数，接收头像二进制数据
 * @return true 表示获取成功且数据非空
 */
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

/**
 * @brief 通过 RPC 调用消息存储服务获取会话的最新消息
 *
 * 调用 message_storage_service 的 GetRecentMsg 接口，
 * 获取指定会话最近的一条消息，用于会话列表展示。
 *
 * @param session_id 会话ID
 * @param msg_info   输出参数，接收最新消息信息
 * @return true 表示会话有消息且获取成功
 */
bool FriendServiceImpl::get_recent_msg(const std::string& session_id, file::MessageInfo& msg_info) {
    if (!channel_pool_ || session_id.empty()) return false;
    auto channel = channel_pool_->get_channel("message_storage_service");
    if (!channel) return false;
    message::MsgStorageService_Stub stub(channel.get());
    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    req.set_request_id(utils::generate_uuid());
    req.set_chat_session_id(session_id);
    req.set_msg_count(1);  // 只需要最新一条
    brpc::Controller cntl;
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed() || !rsp.success()) return false;
    if (rsp.msg_list_size() > 0) {
        msg_info = rsp.msg_list(0);
        return true;
    }
    return false;
}

/**
 * @brief 补充用户头像数据
 *
 * 当用户信息中头像字段为空时，重新调用用户服务获取完整信息，
 * 尝试从头像字段中补充头像数据。
 *
 * @param user_info 用户信息（输入输出参数，可能被修改）
 * @return true 表示头像补充成功
 */
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
// 获取指定用户的所有好友列表
// 流程：查询好友ID -> 批量获取好友信息 -> 补充头像 -> 组装响应
void FriendServiceImpl::GetFriendList(google::protobuf::RpcController* cntl_base,
                                       const friendsvc_ns::GetFriendListReq* request,
                                       friendsvc_ns::GetFriendListRsp* response,
                                       google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);  // 确保函数退出时调用 done
    response->set_request_id(request->request_id());
    response->set_success(false);

    // 参数校验
    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& user_id = request->user_id();

    // 步骤1：从数据库获取所有好友ID
    std::vector<std::string> friend_ids = friend_table_->get_friend_ids(user_id);

    // 步骤2：通过 RPC 批量获取好友的详细信息（昵称、签名等）
    std::map<std::string, file::UserInfo> user_map;
    if (!friend_ids.empty()) {
        get_multi_user_info(friend_ids, user_map);
    }

    // 步骤3：组装响应，为每个好友填充信息并补充头像
    auto* list = response->mutable_friend_list();
    for (const auto& fid : friend_ids) {
        auto* info = list->Add();
        info->set_user_id(fid);
        auto it = user_map.find(fid);
        if (it != user_map.end()) {
            *info = it->second;
            // 若头像为空，尝试重新获取补充
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
// 发送好友申请
// 流程：校验 -> 检查是否已是好友 -> 检查是否已申请 -> 创建申请事件
void FriendServiceImpl::FriendAdd(google::protobuf::RpcController* cntl_base,
                                   const friendsvc_ns::FriendAddReq* request,
                                   friendsvc_ns::FriendAddRsp* response,
                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    // 参数校验
    if (!request->has_user_id() || request->user_id().empty() ||
        request->respondent_id().empty()) {
        response->set_errmsg("Invalid request: missing user_id or respondent_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& req_id = request->user_id();       // 申请人
    const std::string& rsp_id = request->respondent_id();  // 被申请人

    // 不能添加自己为好友
    if (req_id == rsp_id) {
        response->set_errmsg("Cannot add yourself as friend");
        return;
    }

    // 检查1：是否已经是好友关系
    if (friend_table_->is_friend(req_id, rsp_id)) {
        response->set_errmsg("Already friends");
        return;
    }

    // 检查2：是否已有待处理的申请（防止重复申请）
    if (friend_table_->has_pending_event(req_id, rsp_id)) {
        response->set_errmsg("Already applied, please wait");
        return;
    }

    // 创建好友申请事件，生成唯一事件ID
    std::string event_id = "FEVENT_" + utils::generate_uuid();
    if (!friend_table_->add_event(event_id, req_id, rsp_id)) {
        response->set_errmsg("Failed to create friend event");
        return;
    }

    // 返回事件ID，供后续通知和处理使用
    response->set_notify_event_id(event_id);
    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] FriendAdd: {} -> {}, event={}", req_id, rsp_id, event_id);
}

// ==================== GetPendingFriendEventList ====================
// 获取待处理的好友申请列表（即"谁申请加我为好友"）
// 流程：查询待处理事件 -> 批量获取申请者信息 -> 组装响应
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

    // 获取所有发给当前用户且状态为 PENDING 的事件
    auto events = friend_table_->get_pending_events(user_id);

    // 收集所有申请者ID，用于批量获取信息
    std::vector<std::string> applicant_ids;
    std::map<std::string, std::string> event_user_map;  // event_id -> applicant_id
    for (const auto& ev : events) {
        applicant_ids.push_back(ev->req_user_id());
        event_user_map[ev->event_id()] = ev->req_user_id();
    }

    // 批量获取申请者的用户信息
    std::map<std::string, file::UserInfo> user_map;
    if (!applicant_ids.empty()) {
        get_multi_user_info(applicant_ids, user_map);
    }

    // 组装响应：每个事件包含事件ID和申请者信息
    auto* list = response->mutable_event();
    for (const auto& ev : events) {
        auto* fe = list->Add();
        fe->set_event_id(ev->event_id());
        auto it = user_map.find(ev->req_user_id());
        if (it != user_map.end()) {
            *fe->mutable_sender() = it->second;
            // 补充头像
            if (fe->sender().avatar().empty()) {
                fill_user_info_avatar(*fe->mutable_sender());
            }
        } else {
            // 获取不到信息时至少设置用户ID
            fe->mutable_sender()->set_user_id(ev->req_user_id());
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] GetPendingFriendEventList: user={}, events={}",
             user_id, events.size());
}

// ==================== FriendAddProcess ====================
// 处理好友申请：同意或拒绝
// 流程：
//   同意：删除事件 -> 添加好友关系 -> 创建单聊会话 -> 添加会话成员
//   拒绝：删除事件
void FriendServiceImpl::FriendAddProcess(google::protobuf::RpcController* cntl_base,
                                          const friendsvc_ns::FriendAddProcessReq* request,
                                          friendsvc_ns::FriendAddProcessRsp* response,
                                          google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->set_request_id(request->request_id());
    response->set_success(false);

    // 参数校验：需要提供事件ID
    if (request->notify_event_id().empty()) {
        response->set_errmsg("Missing notify_event_id");
        return;
    }
    if (!friend_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    // 根据事件ID查询事件详情
    const std::string& event_id = request->notify_event_id();
    auto event = friend_table_->get_event_by_id(event_id);
    if (!event) {
        response->set_errmsg("Friend event not found");
        return;
    }

    const std::string& req_user_id = event->req_user_id();   // 申请者
    const std::string& rsp_user_id = event->rsp_user_id();   // 被申请者

    // 如果已经是好友了，直接删除事件并返回
    if (friend_table_->is_friend(req_user_id, rsp_user_id)) {
        response->set_errmsg("Already friends");
        friend_table_->remove_event(event_id);
        return;
    }

    // 无论同意还是拒绝，都先删除事件
    friend_table_->remove_event(event_id);

    if (request->agree()) {
        // === 同意好友申请 ===

        // 步骤1：建立双向好友关系
        if (!friend_table_->add_friend_relation(req_user_id, rsp_user_id)) {
            response->set_errmsg("Failed to add friend relation");
            return;
        }

        // 步骤2：创建单聊会话
        std::string session_id = "SESS_" + utils::generate_uuid();
        if (!friend_table_->add_session(session_id, "", session_type_t::SINGLE)) {
            response->set_errmsg("Failed to create session");
            return;
        }

        // 步骤3：将双方都添加为会话成员
        std::vector<std::string> members = {req_user_id, rsp_user_id};
        if (!friend_table_->add_session_members(session_id, members)) {
            response->set_errmsg("Failed to add session members");
            return;
        }

        // 返回新创建的会话ID，供网关发送通知
        response->set_new_session_id(session_id);
        LOG_INFO("[FriendServiceImpl] FriendAddProcess accept: {} <-> {}, session={}",
                 req_user_id, rsp_user_id, session_id);
    } else {
        // === 拒绝好友申请（仅记录日志，事件已删除） ===
        LOG_INFO("[FriendServiceImpl] FriendAddProcess reject: {} -> {}", req_user_id, rsp_user_id);
    }

    response->set_success(true);
}

// ==================== FriendRemove ====================
// 删除好友关系，同时删除两人之间的单聊会话
// 流程：删除好友关系 -> 查找共同会话 -> 删除单聊会话及其成员
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

    // 步骤1：删除双向好友关系
    friend_table_->remove_friend_relation(user_id, peer_id);

    // 步骤2：查找两人共同的会话（求交集）
    auto user_sessions = friend_table_->get_session_ids_by_user(user_id);
    auto peer_sessions = friend_table_->get_session_ids_by_user(peer_id);

    for (const auto& sid : user_sessions) {
        // 检查该会话是否也是对方的会话
        bool is_common = false;
        for (const auto& psid : peer_sessions) {
            if (sid == psid) { is_common = true; break; }
        }
        if (!is_common) continue;

        auto session = friend_table_->get_session_by_id(sid);
        if (!session) continue;

        // 获取会话成员，判断是否为两人的单聊会话
        auto members = friend_table_->get_session_member_ids(sid);
        if (members.size() == 2) {
            bool contains_both = false;
            bool has_user = false, has_peer = false;
            for (const auto& m : members) {
                if (m == user_id) has_user = true;
                if (m == peer_id) has_peer = true;
            }
            contains_both = has_user && has_peer;
            // 确认是两人的单聊会话后删除
            if (contains_both && session->session_type() == session_type_t::SINGLE) {
                friend_table_->remove_all_session_members(sid);
                friend_table_->remove_session(sid);
                LOG_INFO("[FriendServiceImpl] FriendRemove removed session: {}", sid);
            }
        }
    }

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] FriendRemove: {} <-> {}", user_id, peer_id);
}

// ==================== FriendSearch ====================
// 根据关键字搜索用户（排除自己和已有好友）
// 流程：收集排除列表 -> ES搜索 -> 获取头像 -> 组装响应
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

    // 收集需要排除的用户ID：自己 + 所有已有好友
    std::vector<std::string> exclude_uids;
    exclude_uids.push_back(user_id);
    auto friend_ids = friend_table_ ? friend_table_->get_friend_ids(user_id)
                                    : std::vector<std::string>();
    for (const auto& fid : friend_ids) {
        exclude_uids.push_back(fid);
    }

    // 调用 Elasticsearch 进行用户搜索
    std::vector<friend_es::ESUser> es_users;
    if (!friend_es_->search_user(keyword, exclude_uids, es_users)) {
        response->set_errmsg("Search failed");
        return;
    }

    // 组装响应：为每个搜索结果填充用户信息并获取头像
    auto* list = response->mutable_user_info();
    std::vector<std::string> ids_to_fetch_avatar;
    for (const auto& u : es_users) {
        auto* info = list->Add();
        info->set_user_id(u.user_id);
        info->set_nickname(u.nickname);
        info->set_email(u.email);
        info->set_description(u.description);
        // 若有头像ID，通过文件服务获取头像数据
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
// 创建群聊会话
// 流程：校验成员列表 -> 创建会话 -> 添加成员 -> 组装响应
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

    // 从请求中提取成员ID列表
    std::vector<std::string> members;
    for (int i = 0; i < request->member_id_list_size(); ++i) {
        members.push_back(request->member_id_list(i));
    }

    if (members.empty()) {
        response->set_errmsg("Empty member list");
        return;
    }

    // 生成会话唯一标识
    std::string session_id = "SESS_" + utils::generate_uuid();

    // 创建群聊会话记录
    if (!friend_table_->add_session(session_id, session_name, session_type_t::GROUP)) {
        response->set_errmsg("Failed to create session");
        return;
    }

    // 将所有成员添加到会话中
    if (!friend_table_->add_session_members(session_id, members)) {
        response->set_errmsg("Failed to add members");
        return;
    }

    // 组装响应中的会话信息
    auto* session_info = response->mutable_chat_session_info();
    session_info->set_chat_session_id(session_id);
    session_info->set_chat_session_name(session_name);

    response->set_success(true);
    LOG_INFO("[FriendServiceImpl] ChatSessionCreate: id={}, name={}, members={}",
             session_id, session_name, members.size());
}

// ==================== GetChatSessionMember ====================
// 获取指定会话的成员列表
// 流程：查询成员ID -> 批量获取成员信息 -> 补充头像 -> 组装响应
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

    // 获取会话所有成员ID
    auto member_ids = friend_table_->get_session_member_ids(session_id);

    // 批量获取成员详细信息
    std::map<std::string, file::UserInfo> user_map;
    if (!member_ids.empty()) {
        get_multi_user_info(member_ids, user_map);
    }

    // 组装响应：填充每个成员的信息和头像
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
// 获取指定用户参与的所有聊天会话列表
// 流程：查询用户会话 -> 逐个处理会话信息 -> 获取最新消息 -> 组装响应
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

    // 获取用户参与的所有会话ID
    auto session_ids = friend_table_->get_session_ids_by_user(user_id);

    auto* list = response->mutable_chat_session_info_list();

    // 逐个会话处理，填充会话信息
    for (const auto& sid : session_ids) {
        auto session = friend_table_->get_session_by_id(sid);
        if (!session) continue;

        auto* info = list->Add();
        info->set_chat_session_id(sid);

        // 获取会话成员列表
        auto member_ids = friend_table_->get_session_member_ids(sid);

        if (session->session_type() == session_type_t::SINGLE) {
            // === 单聊会话 ===
            // 找到对方用户ID（非当前用户的那个成员）
            std::string peer_id;
            for (const auto& mid : member_ids) {
                if (mid != user_id) { peer_id = mid; break; }
            }
            info->set_single_chat_friend_id(peer_id);

            // 获取对方信息，用对方昵称作为会话名，用对方头像作为会话头像
            file::UserInfo peer_info;
            if (!peer_id.empty() && get_user_info(peer_id, peer_info)) {
                info->set_chat_session_name(peer_info.nickname());
                if (!peer_info.avatar().empty()) {
                    info->set_avatar(peer_info.avatar());
                } else {
                    // 若头像为空，再次尝试获取完整信息
                    file::UserInfo full_peer;
                    if (get_user_info(peer_id, full_peer) && !full_peer.avatar().empty()) {
                        info->set_avatar(full_peer.avatar());
                    }
                }
            } else {
                // 获取不到对方信息时，使用对方用户ID作为会话名
                info->set_chat_session_name(peer_id);
            }
        } else {
            // === 群聊会话 ===
            std::string name = session->session_name().null() ? "" : session->session_name().get();
            info->set_chat_session_name(name.empty() ? "群聊" : name);
        }

        // 获取会话最新一条消息（用于会话列表预览）
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
