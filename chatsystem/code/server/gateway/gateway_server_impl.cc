// =============================================================================
// gateway_server_impl.cc - 网关服务器核心实现
// =============================================================================
// 本文件实现 GatewayServerImpl 类的所有方法，包括：
//   1. HTTP 服务器路由注册与请求处理
//   2. WebSocket 服务器事件处理
//   3. RPC 服务调用转发
//   4. 会话鉴权验证
//   5. 事件通知推送
//
// 实现流程（参考 gateway.md 设计文档）：
//   - 用户注册/登录：HTTP请求 -> 反序列化 -> 调用用户服务 -> 返回会话ID
//   - 需要鉴权的接口：HTTP请求 -> 反序列化 -> Redis鉴权 -> 调用子服务 -> 返回结果
//   - 事件通知：子服务调用成功后 -> 组装通知消息 -> WebSocket推送给目标用户
//
// 文件结构（模板需先定义后使用）：
//   1. ProtoBuf 反射辅助方法
//   2. 构造与析构
//   3. RPC 调用模板方法（call_*_service）
//   4. HTTP 请求处理模板（handle_rpc_request）
//   5. 会话鉴权
//   6. 通知推送方法
//   7. 初始化方法（init）
//   8. 启动与停止方法（start/stop）
// =============================================================================

#include "gateway_server_impl.hpp"

#include <sstream>
#include <atomic>
#include <chrono>
#include <google/protobuf/reflection.h>

namespace gateway {

// =============================================================================
// 辅助函数
// =============================================================================

/// 生成请求ID，用于内部 RPC 调用追踪
static std::string generate_request_id() {
    static std::atomic<uint64_t> counter(0);
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "gw_" + std::to_string(ms) + "_" + std::to_string(counter++);
}

// =============================================================================
// ProtoBuf 反射辅助方法
// =============================================================================

bool GatewayServerImpl::get_session_id_from_proto(const google::protobuf::Message& msg,
                                                   std::string& session_id) {
    const google::protobuf::Reflection* reflection = msg.GetReflection();
    const google::protobuf::Descriptor* descriptor = msg.GetDescriptor();

    // 查找 session_id 字段
    const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName("session_id");
    if (!field || field->type() != google::protobuf::FieldDescriptor::TYPE_STRING) {
        LOG_DEBUG("[GatewayServerImpl] session_id field not found in protobuf message");
        return false;
    }

    // 获取字段值
    if (field->is_optional() && !reflection->HasField(msg, field)) {
        LOG_DEBUG("[GatewayServerImpl] session_id field not set in protobuf message");
        return false;
    }

    session_id = reflection->GetString(msg, field);
    return true;
}

bool GatewayServerImpl::set_user_id_to_proto(google::protobuf::Message& msg,
                                               const std::string& user_id) {
    const google::protobuf::Reflection* reflection = msg.GetReflection();
    const google::protobuf::Descriptor* descriptor = msg.GetDescriptor();

    // 查找 user_id 字段
    const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName("user_id");
    if (!field || field->type() != google::protobuf::FieldDescriptor::TYPE_STRING) {
        LOG_DEBUG("[GatewayServerImpl] user_id field not found in protobuf message");
        return false;
    }

    // 设置字段值
    reflection->SetString(&msg, field, user_id);
    return true;
}

// =============================================================================
// 构造与析构
// =============================================================================

GatewayServerImpl::GatewayServerImpl()
    : http_port_(8888), ws_port_(8889), running_(false) {
}

GatewayServerImpl::~GatewayServerImpl() {
    stop();
}

// =============================================================================
// RPC 调用辅助方法实现（必须在模板实例化之前定义）
// =============================================================================

template<typename Req, typename Rsp>
bool GatewayServerImpl::call_user_service(const std::string& rpc_name, const Req& req, Rsp& rsp) {
    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[GatewayServerImpl] No channel available for user_service");
        return false;
    }

    chat::UserService_Stub stub(channel.get());
    brpc::Controller cntl;

    if (rpc_name == "UserRegister") {
        stub.UserRegister(&cntl, reinterpret_cast<const chat::UserRegisterReq*>(&req),
                          reinterpret_cast<chat::UserRegisterRsp*>(&rsp), nullptr);
    } else if (rpc_name == "UserLogin") {
        stub.UserLogin(&cntl, reinterpret_cast<const chat::UserLoginReq*>(&req),
                       reinterpret_cast<chat::UserLoginRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetEmailVerifyCode") {
        stub.GetEmailVerifyCode(&cntl, reinterpret_cast<const chat::EmailVerifyCodeReq*>(&req),
                                reinterpret_cast<chat::EmailVerifyCodeRsp*>(&rsp), nullptr);
    } else if (rpc_name == "EmailRegister") {
        stub.EmailRegister(&cntl, reinterpret_cast<const chat::EmailRegisterReq*>(&req),
                           reinterpret_cast<chat::EmailRegisterRsp*>(&rsp), nullptr);
    } else if (rpc_name == "EmailLogin") {
        stub.EmailLogin(&cntl, reinterpret_cast<const chat::EmailLoginReq*>(&req),
                        reinterpret_cast<chat::EmailLoginRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetUserInfo") {
        stub.GetUserInfo(&cntl, reinterpret_cast<const chat::GetUserInfoReq*>(&req),
                         reinterpret_cast<chat::GetUserInfoRsp*>(&rsp), nullptr);
    } else if (rpc_name == "SetUserAvatar") {
        stub.SetUserAvatar(&cntl, reinterpret_cast<const chat::SetUserAvatarReq*>(&req),
                           reinterpret_cast<chat::SetUserAvatarRsp*>(&rsp), nullptr);
    } else if (rpc_name == "SetUserNickname") {
        stub.SetUserNickname(&cntl, reinterpret_cast<const chat::SetUserNicknameReq*>(&req),
                             reinterpret_cast<chat::SetUserNicknameRsp*>(&rsp), nullptr);
    } else if (rpc_name == "SetUserDescription") {
        stub.SetUserDescription(&cntl, reinterpret_cast<const chat::SetUserDescriptionReq*>(&req),
                                 reinterpret_cast<chat::SetUserDescriptionRsp*>(&rsp), nullptr);
    } else if (rpc_name == "SetUserEmail") {
        stub.SetUserEmail(&cntl, reinterpret_cast<const chat::SetUserEmailReq*>(&req),
                          reinterpret_cast<chat::SetUserEmailRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetMultiUserInfo") {
        stub.GetMultiUserInfo(&cntl, reinterpret_cast<const chat::GetMultiUserInfoReq*>(&req),
                              reinterpret_cast<chat::GetMultiUserInfoRsp*>(&rsp), nullptr);
    }

    if (cntl.Failed()) {
        LOG_ERROR("[GatewayServerImpl] RPC call '{}' to user_service failed: {}", rpc_name, cntl.ErrorText());
        return false;
    }
    return true;
}

template<typename Req, typename Rsp>
bool GatewayServerImpl::call_friend_service(const std::string& rpc_name, const Req& req, Rsp& rsp) {
    auto channel = channel_pool_->get_channel("friend_service");
    if (!channel) {
        LOG_ERROR("[GatewayServerImpl] No channel available for friend_service");
        return false;
    }

    friendsvc::FriendService_Stub stub(channel.get());
    brpc::Controller cntl;

    if (rpc_name == "GetFriendList") {
        stub.GetFriendList(&cntl, reinterpret_cast<const friendsvc::GetFriendListReq*>(&req),
                           reinterpret_cast<friendsvc::GetFriendListRsp*>(&rsp), nullptr);
    } else if (rpc_name == "FriendRemove") {
        stub.FriendRemove(&cntl, reinterpret_cast<const friendsvc::FriendRemoveReq*>(&req),
                          reinterpret_cast<friendsvc::FriendRemoveRsp*>(&rsp), nullptr);
    } else if (rpc_name == "FriendAdd") {
        stub.FriendAdd(&cntl, reinterpret_cast<const friendsvc::FriendAddReq*>(&req),
                       reinterpret_cast<friendsvc::FriendAddRsp*>(&rsp), nullptr);
    } else if (rpc_name == "FriendAddProcess") {
        stub.FriendAddProcess(&cntl, reinterpret_cast<const friendsvc::FriendAddProcessReq*>(&req),
                              reinterpret_cast<friendsvc::FriendAddProcessRsp*>(&rsp), nullptr);
    } else if (rpc_name == "FriendSearch") {
        stub.FriendSearch(&cntl, reinterpret_cast<const friendsvc::FriendSearchReq*>(&req),
                          reinterpret_cast<friendsvc::FriendSearchRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetChatSessionList") {
        stub.GetChatSessionList(&cntl, reinterpret_cast<const friendsvc::GetChatSessionListReq*>(&req),
                                reinterpret_cast<friendsvc::GetChatSessionListRsp*>(&rsp), nullptr);
    } else if (rpc_name == "ChatSessionCreate") {
        stub.ChatSessionCreate(&cntl, reinterpret_cast<const friendsvc::ChatSessionCreateReq*>(&req),
                               reinterpret_cast<friendsvc::ChatSessionCreateRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetChatSessionMember") {
        stub.GetChatSessionMember(&cntl, reinterpret_cast<const friendsvc::GetChatSessionMemberReq*>(&req),
                                  reinterpret_cast<friendsvc::GetChatSessionMemberRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetPendingFriendEventList") {
        stub.GetPendingFriendEventList(&cntl,
            reinterpret_cast<const friendsvc::GetPendingFriendEventListReq*>(&req),
            reinterpret_cast<friendsvc::GetPendingFriendEventListRsp*>(&rsp), nullptr);
    }

    if (cntl.Failed()) {
        LOG_ERROR("[GatewayServerImpl] RPC call '{}' to friend_service failed: {}",
                  rpc_name, cntl.ErrorText());
        return false;
    }
    return true;
}

template<typename Req, typename Rsp>
bool GatewayServerImpl::call_msg_storage_service(const std::string& rpc_name, const Req& req, Rsp& rsp) {
    auto channel = channel_pool_->get_channel("message_storage_service");
    if (!channel) {
        LOG_ERROR("[GatewayServerImpl] No channel available for message_storage_service");
        return false;
    }

    message::MsgStorageService_Stub stub(channel.get());
    brpc::Controller cntl;

    if (rpc_name == "GetHistoryMsg") {
        stub.GetHistoryMsg(&cntl, reinterpret_cast<const message::GetHistoryMsgReq*>(&req),
                           reinterpret_cast<message::GetHistoryMsgRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetRecentMsg") {
        stub.GetRecentMsg(&cntl, reinterpret_cast<const message::GetRecentMsgReq*>(&req),
                          reinterpret_cast<message::GetRecentMsgRsp*>(&rsp), nullptr);
    } else if (rpc_name == "MsgSearch") {
        stub.MsgSearch(&cntl, reinterpret_cast<const message::MsgSearchReq*>(&req),
                       reinterpret_cast<message::MsgSearchRsp*>(&rsp), nullptr);
    }

    if (cntl.Failed()) {
        LOG_ERROR("[GatewayServerImpl] RPC call '{}' to message_storage_service failed: {}",
                  rpc_name, cntl.ErrorText());
        return false;
    }
    return true;
}

template<typename Req, typename Rsp>
bool GatewayServerImpl::call_msg_transmit_service(const std::string& rpc_name, const Req& req, Rsp& rsp) {
    auto channel = channel_pool_->get_channel("transmit_service");
    if (!channel) {
        LOG_ERROR("[GatewayServerImpl] No channel available for transmit_service");
        return false;
    }

    transmit::MsgTransmitService_Stub stub(channel.get());
    brpc::Controller cntl;

    if (rpc_name == "GetTransmitTarget") {
        stub.GetTransmitTarget(&cntl, reinterpret_cast<const transmit::NewMessageReq*>(&req),
                               reinterpret_cast<transmit::GetTransmitTargetRsp*>(&rsp), nullptr);
    }

    if (cntl.Failed()) {
        LOG_ERROR("[GatewayServerImpl] RPC call '{}' to transmit_service failed: {}",
                  rpc_name, cntl.ErrorText());
        return false;
    }
    return true;
}

template<typename Req, typename Rsp>
bool GatewayServerImpl::call_file_service(const std::string& rpc_name, const Req& req, Rsp& rsp) {
    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) {
        LOG_ERROR("[GatewayServerImpl] No channel available for file_service");
        return false;
    }

    file::FileService_Stub stub(channel.get());
    brpc::Controller cntl;

    if (rpc_name == "GetSingleFile") {
        stub.GetSingleFile(&cntl, reinterpret_cast<const file::GetSingleFileReq*>(&req),
                           reinterpret_cast<file::GetSingleFileRsp*>(&rsp), nullptr);
    } else if (rpc_name == "GetMultiFile") {
        stub.GetMultiFile(&cntl, reinterpret_cast<const file::GetMultiFileReq*>(&req),
                          reinterpret_cast<file::GetMultiFileRsp*>(&rsp), nullptr);
    } else if (rpc_name == "PutSingleFile") {
        stub.PutSingleFile(&cntl, reinterpret_cast<const file::PutSingleFileReq*>(&req),
                           reinterpret_cast<file::PutSingleFileRsp*>(&rsp), nullptr);
    } else if (rpc_name == "PutMultiFile") {
        stub.PutMultiFile(&cntl, reinterpret_cast<const file::PutMultiFileReq*>(&req),
                          reinterpret_cast<file::PutMultiFileRsp*>(&rsp), nullptr);
    }

    if (cntl.Failed()) {
        LOG_ERROR("[GatewayServerImpl] RPC call '{}' to file_service failed: {}", rpc_name, cntl.ErrorText());
        return false;
    }
    return true;
}

template<typename Req, typename Rsp>
bool GatewayServerImpl::call_speech_service(const std::string& rpc_name, const Req& req, Rsp& rsp) {
    auto channel = channel_pool_->get_channel("speech_service");
    if (!channel) {
        LOG_ERROR("[GatewayServerImpl] No channel available for speech_service");
        return false;
    }

    speech::SpeechService_Stub stub(channel.get());
    brpc::Controller cntl;

    if (rpc_name == "SpeechRecognition") {
        stub.SpeechRecognition(&cntl,
            reinterpret_cast<const speech::SpeechRecognitionReq*>(&req),
            reinterpret_cast<speech::SpeechRecognitionRsp*>(&rsp), nullptr);
    }

    if (cntl.Failed()) {
        LOG_ERROR("[GatewayServerImpl] RPC call '{}' to speech_service failed: {}",
                  rpc_name, cntl.ErrorText());
        return false;
    }
    return true;
}

// =============================================================================
// HTTP 请求处理模板（必须在模板实例化之前定义）
// =============================================================================

template<typename ReqProto, typename RspProto>
void GatewayServerImpl::handle_rpc_request(
    const httplib::Request& req, httplib::Response& rsp,
    bool require_auth,
    std::function<void(const ReqProto&, RspProto&)> rpc_handler) {

    // 1. 解析请求体
    ReqProto req_proto;
    if (!req_proto.ParseFromString(req.body)) {
        LOG_ERROR("[GatewayServerImpl] Failed to parse request body");
        rsp.status = 400;
        rsp.set_content("Invalid request body", "text/plain");
        return;
    }

    // 2. 会话鉴权（如果需要）
    if (require_auth) {
        std::string user_id;
        std::string session_id;
        // 使用反射获取 session_id
        get_session_id_from_proto(req_proto, session_id);
        if (!authenticate_session(session_id, user_id)) {
            LOG_WARN("[GatewayServerImpl] Authentication failed for session: {}", session_id);
            rsp.status = 401;
            rsp.set_content("Authentication failed", "text/plain");
            return;
        }
        // 使用反射设置 user_id
        set_user_id_to_proto(req_proto, user_id);
    }

    // 3. 调用业务处理
    RspProto rsp_proto;
    try {
        rpc_handler(req_proto, rsp_proto);
    } catch (const std::exception& e) {
        LOG_ERROR("[GatewayServerImpl] RPC handler exception: {}", e.what());
        rsp.status = 500;
        rsp.set_content("Internal server error", "text/plain");
        return;
    }

    // 4. 序列化响应
    std::string rsp_body;
    if (!rsp_proto.SerializeToString(&rsp_body)) {
        LOG_ERROR("[GatewayServerImpl] Failed to serialize response");
        rsp.status = 500;
        rsp.set_content("Failed to serialize response", "text/plain");
        return;
    }

    rsp.set_content(rsp_body, "application/x-protobuf");
}

// =============================================================================
// 会话鉴权
// =============================================================================

bool GatewayServerImpl::authenticate_session(const std::string& session_id, std::string& user_id) {
    if (!redis_client_ || session_id.empty()) {
        LOG_WARN("[GatewayServerImpl] Redis client not initialized or empty session_id");
        return false;
    }

    user_id = redis_client_->get_user_id_by_session(session_id);
    if (user_id.empty()) {
        LOG_WARN("[GatewayServerImpl] Session not found or expired: {}", session_id);
        return false;
    }

    // 检查用户是否已登录
    if (!redis_client_->is_user_logged_in(user_id)) {
        LOG_WARN("[GatewayServerImpl] User not logged in: {}", user_id);
        return false;
    }

    LOG_DEBUG("[GatewayServerImpl] Session authenticated: {} -> {}", session_id, user_id);
    return true;
}

// =============================================================================
// 通知推送方法
// =============================================================================

void GatewayServerImpl::push_friend_apply_notify(const std::string& target_user_id,
                                                   const file::UserInfo& applicant_info) {
    // 构建通知消息
    notify::NotifyMessage notify_msg;
    notify_msg.set_notify_type(notify::FRIEND_ADD_APPLY_NOTIFY);
    auto* apply = notify_msg.mutable_friend_add_apply();
    apply->mutable_user_info()->CopyFrom(applicant_info);

    std::string json_msg = notify_msg.SerializeAsString();
    int pushed = conn_manager_.push_notification(target_user_id, json_msg);
    if (pushed > 0) {
        LOG_INFO("[GatewayServerImpl] Friend apply notify pushed to user: {}", target_user_id);
    }
}

void GatewayServerImpl::push_friend_process_notify(const std::string& target_user_id,
                                                     bool agree,
                                                     const file::UserInfo& handler_info) {
    notify::NotifyMessage notify_msg;
    notify_msg.set_notify_type(notify::FRIEND_ADD_PROCESS_NOTIFY);
    auto* process = notify_msg.mutable_friend_process_result();
    process->set_agree(agree);
    process->mutable_user_info()->CopyFrom(handler_info);

    std::string json_msg = notify_msg.SerializeAsString();
    int pushed = conn_manager_.push_notification(target_user_id, json_msg);
    if (pushed > 0) {
        LOG_INFO("[GatewayServerImpl] Friend process notify pushed to user: {}", target_user_id);
    }
}

void GatewayServerImpl::push_friend_remove_notify(const std::string& target_user_id) {
    notify::NotifyMessage notify_msg;
    notify_msg.set_notify_type(notify::FRIEND_REMOVE_NOTIFY);
    auto* remove = notify_msg.mutable_friend_remove();
    remove->set_user_id(target_user_id);

    std::string json_msg = notify_msg.SerializeAsString();
    int pushed = conn_manager_.push_notification(target_user_id, json_msg);
    if (pushed > 0) {
        LOG_INFO("[GatewayServerImpl] Friend remove notify pushed to user: {}", target_user_id);
    }
}

void GatewayServerImpl::push_chat_session_create_notify(
    const std::vector<std::string>& member_ids,
    const file::ChatSessionInfo& session_info) {
    notify::NotifyMessage notify_msg;
    notify_msg.set_notify_type(notify::CHAT_SESSION_CREATE_NOTIFY);
    auto* session = notify_msg.mutable_new_chat_session_info();
    session->mutable_chat_session_info()->CopyFrom(session_info);

    std::string json_msg = notify_msg.SerializeAsString();
    for (const auto& member_id : member_ids) {
        int pushed = conn_manager_.push_notification(member_id, json_msg);
        if (pushed > 0) {
            LOG_INFO("[GatewayServerImpl] Session create notify pushed to user: {}", member_id);
        }
    }
}

void GatewayServerImpl::push_new_message_notify(
    const std::vector<std::string>& target_user_ids,
    const file::MessageInfo& message_info) {
    notify::NotifyMessage notify_msg;
    notify_msg.set_notify_type(notify::CHAT_MESSAGE_NOTIFY);
    auto* msg = notify_msg.mutable_new_message_info();
    msg->mutable_message_info()->CopyFrom(message_info);

    std::string json_msg = notify_msg.SerializeAsString();
    for (const auto& user_id : target_user_ids) {
        int pushed = conn_manager_.push_notification(user_id, json_msg);
        if (pushed > 0) {
            LOG_INFO("[GatewayServerImpl] New message notify pushed to user: {}", user_id);
        }
    }
}

// =============================================================================
// 初始化
// =============================================================================

void GatewayServerImpl::init(int http_port, int ws_port,
                              std::shared_ptr<redis_client::RedisClient> redis_client,
                              std::shared_ptr<brpc::ServiceChannelPool> channel_pool) {
    http_port_ = http_port;
    ws_port_ = ws_port;
    redis_client_ = redis_client;
    channel_pool_ = channel_pool;

    // 初始化WebSocket服务器
    ws_server_.set_access_channels(websocketpp::log::alevel::none);
    ws_server_.set_error_channels(websocketpp::log::elevel::none);

    // WebSocket连接建立回调
    ws_server_.set_open_handler([this](websocketpp::connection_hdl hdl) {
        LOG_INFO("[GatewayServerImpl] WebSocket connection opened");
    });

    // WebSocket连接断开回调
    ws_server_.set_close_handler([this](websocketpp::connection_hdl hdl) {
        LOG_INFO("[GatewayServerImpl] WebSocket connection closed");
    });

    // WebSocket消息接收回调
    ws_server_.set_message_handler([this](websocketpp::connection_hdl hdl,
                                           WSServer::message_ptr msg) {
        LOG_DEBUG("[GatewayServerImpl] WebSocket message received: {}", msg->get_payload());
    });

    // 初始化HTTP路由
    // ---------- 用户服务路由 ----------

    // 用户注册（无需鉴权）
    http_server_.Post("/api/user/register",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::UserRegisterReq, chat::UserRegisterRsp>(
                req, rsp, false,
                [this](const chat::UserRegisterReq& req, chat::UserRegisterRsp& rsp) {
                    call_user_service("UserRegister", req, rsp);
                });
        });

    // 用户登录（无需鉴权）
    http_server_.Post("/api/user/login",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::UserLoginReq, chat::UserLoginRsp>(
                req, rsp, false,
                [this](const chat::UserLoginReq& req, chat::UserLoginRsp& rsp) {
                    call_user_service("UserLogin", req, rsp);
                });
        });

    // 邮箱验证码获取（无需鉴权）
    http_server_.Post("/api/user/email/verify_code",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::EmailVerifyCodeReq, chat::EmailVerifyCodeRsp>(
                req, rsp, false,
                [this](const chat::EmailVerifyCodeReq& req, chat::EmailVerifyCodeRsp& rsp) {
                    call_user_service("GetEmailVerifyCode", req, rsp);
                });
        });

    // 邮箱注册（无需鉴权）
    http_server_.Post("/api/user/email/register",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::EmailRegisterReq, chat::EmailRegisterRsp>(
                req, rsp, false,
                [this](const chat::EmailRegisterReq& req, chat::EmailRegisterRsp& rsp) {
                    call_user_service("EmailRegister", req, rsp);
                });
        });

    // 邮箱登录（无需鉴权）
    http_server_.Post("/api/user/email/login",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::EmailLoginReq, chat::EmailLoginRsp>(
                req, rsp, false,
                [this](const chat::EmailLoginReq& req, chat::EmailLoginRsp& rsp) {
                    call_user_service("EmailLogin", req, rsp);
                });
        });

    // 获取用户信息（需鉴权）
    http_server_.Post("/api/user/info",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::GetUserInfoReq, chat::GetUserInfoRsp>(
                req, rsp, true,
                [this](const chat::GetUserInfoReq& req, chat::GetUserInfoRsp& rsp) {
                    call_user_service("GetUserInfo", req, rsp);
                });
        });

    // 修改用户头像（需鉴权）
    http_server_.Post("/api/user/avatar",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::SetUserAvatarReq, chat::SetUserAvatarRsp>(
                req, rsp, true,
                [this](const chat::SetUserAvatarReq& req, chat::SetUserAvatarRsp& rsp) {
                    call_user_service("SetUserAvatar", req, rsp);
                });
        });

    // 修改用户签名（需鉴权）
    http_server_.Post("/api/user/signature",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::SetUserDescriptionReq, chat::SetUserDescriptionRsp>(
                req, rsp, true,
                [this](const chat::SetUserDescriptionReq& req, chat::SetUserDescriptionRsp& rsp) {
                    call_user_service("SetUserDescription", req, rsp);
                });
        });

    // 修改用户昵称（需鉴权）
    http_server_.Post("/api/user/nickname",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::SetUserNicknameReq, chat::SetUserNicknameRsp>(
                req, rsp, true,
                [this](const chat::SetUserNicknameReq& req, chat::SetUserNicknameRsp& rsp) {
                    call_user_service("SetUserNickname", req, rsp);
                });
        });

    // 修改用户绑定邮箱（需鉴权）
    http_server_.Post("/api/user/email/bind",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<chat::SetUserEmailReq, chat::SetUserEmailRsp>(
                req, rsp, true,
                [this](const chat::SetUserEmailReq& req, chat::SetUserEmailRsp& rsp) {
                    call_user_service("SetUserEmail", req, rsp);
                });
        });

    // ---------- 好友服务路由 ----------

    // 获取好友列表（需鉴权）
    http_server_.Post("/api/friend/list",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::GetFriendListReq, friendsvc::GetFriendListRsp>(
                req, rsp, true,
                [this](const friendsvc::GetFriendListReq& req, friendsvc::GetFriendListRsp& rsp) {
                    call_friend_service("GetFriendList", req, rsp);
                });
        });

    // 发送好友申请（需鉴权）
    http_server_.Post("/api/friend/apply",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::FriendAddReq, friendsvc::FriendAddRsp>(
                req, rsp, true,
                [this](const friendsvc::FriendAddReq& req, friendsvc::FriendAddRsp& rsp) {
                    // 1. 调用好友服务发送申请
                    call_friend_service("FriendAdd", req, rsp);

                    // 2. 如果成功，获取被申请人信息并推送通知
                    if (rsp.success()) {
                        // 调用用户服务获取被申请人信息
                        chat::GetUserInfoReq info_req;
                        chat::GetUserInfoRsp info_rsp;
                        info_req.set_request_id(generate_request_id());
                        info_req.set_user_id(req.respondent_id());
                        if (call_user_service("GetUserInfo", info_req, info_rsp) && info_rsp.success()) {
                            push_friend_apply_notify(req.respondent_id(), info_rsp.user_info());
                        }
                    }
                });
        });

    // 获取待处理好友申请（需鉴权）
    http_server_.Post("/api/friend/pending",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::GetPendingFriendEventListReq,
                              friendsvc::GetPendingFriendEventListRsp>(
                req, rsp, true,
                [this](const friendsvc::GetPendingFriendEventListReq& req,
                       friendsvc::GetPendingFriendEventListRsp& rsp) {
                    call_friend_service("GetPendingFriendEventList", req, rsp);
                });
        });

    // 好友申请处理（需鉴权）
    http_server_.Post("/api/friend/process",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::FriendAddProcessReq, friendsvc::FriendAddProcessRsp>(
                req, rsp, true,
                [this](const friendsvc::FriendAddProcessReq& req, friendsvc::FriendAddProcessRsp& rsp) {
                    // 1. 调用好友服务处理申请
                    call_friend_service("FriendAddProcess", req, rsp);

                    // 2. 如果处理成功，通知申请人处理结果
                    if (rsp.success()) {
                        // 获取处理人信息并推送通知
                        chat::GetUserInfoReq handler_info_req;
                        chat::GetUserInfoRsp handler_info_rsp;
                        handler_info_req.set_request_id(generate_request_id());
                        handler_info_req.set_user_id(req.user_id());
                        if (call_user_service("GetUserInfo", handler_info_req, handler_info_rsp)
                            && handler_info_rsp.success()) {
                            push_friend_process_notify(req.apply_user_id(), req.agree(),
                                                       handler_info_rsp.user_info());
                        }

                        // 3. 如果同意，通知双方新会话创建
                        if (req.agree() && rsp.has_new_session_id()) {
                            std::vector<std::string> member_ids = {req.user_id(), req.apply_user_id()};

                            // 构建会话信息
                            file::ChatSessionInfo session_info;
                            session_info.set_chat_session_id(rsp.new_session_id());

                            // 获取申请人信息作为会话名称和头像
                            chat::GetUserInfoReq applicant_info_req;
                            chat::GetUserInfoRsp applicant_info_rsp;
                            applicant_info_req.set_request_id(generate_request_id());
                            applicant_info_req.set_user_id(req.apply_user_id());
                            if (call_user_service("GetUserInfo", applicant_info_req, applicant_info_rsp)
                                && applicant_info_rsp.success()) {
                                session_info.set_chat_session_name(applicant_info_rsp.user_info().nickname());
                                session_info.set_avatar(applicant_info_rsp.user_info().avatar());
                            }

                            push_chat_session_create_notify(member_ids, session_info);

                            // 清理响应中的会话ID信息（客户端不需要关注）
                            rsp.clear_new_session_id();
                        }
                    }
                });
        });

    // 删除好友（需鉴权）
    http_server_.Post("/api/friend/remove",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::FriendRemoveReq, friendsvc::FriendRemoveRsp>(
                req, rsp, true,
                [this](const friendsvc::FriendRemoveReq& req, friendsvc::FriendRemoveRsp& rsp) {
                    call_friend_service("FriendRemove", req, rsp);

                    // 如果成功，通知被删除者
                    if (rsp.success()) {
                        push_friend_remove_notify(req.peer_id());
                    }
                });
        });

    // 搜索用户（需鉴权）
    http_server_.Post("/api/friend/search",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::FriendSearchReq, friendsvc::FriendSearchRsp>(
                req, rsp, true,
                [this](const friendsvc::FriendSearchReq& req, friendsvc::FriendSearchRsp& rsp) {
                    call_friend_service("FriendSearch", req, rsp);
                });
        });

    // 获取聊天会话列表（需鉴权）
    http_server_.Post("/api/friend/session/list",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::GetChatSessionListReq, friendsvc::GetChatSessionListRsp>(
                req, rsp, true,
                [this](const friendsvc::GetChatSessionListReq& req, friendsvc::GetChatSessionListRsp& rsp) {
                    call_friend_service("GetChatSessionList", req, rsp);
                });
        });

    // 创建多人聊天会话（需鉴权）
    http_server_.Post("/api/friend/session/create",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::ChatSessionCreateReq, friendsvc::ChatSessionCreateRsp>(
                req, rsp, true,
                [this](const friendsvc::ChatSessionCreateReq& req, friendsvc::ChatSessionCreateRsp& rsp) {
                    call_friend_service("ChatSessionCreate", req, rsp);

                    // 如果成功，循环通知所有会话成员
                    if (rsp.success() && rsp.has_chat_session_info()) {
                        std::vector<std::string> member_ids(req.member_id_list().begin(),
                                                           req.member_id_list().end());
                        push_chat_session_create_notify(member_ids, rsp.chat_session_info());

                        // 清理响应中的会话信息（通过通知发送）
                        rsp.clear_chat_session_info();
                    }
                });
        });

    // 获取会话成员列表（需鉴权）
    http_server_.Post("/api/friend/session/members",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<friendsvc::GetChatSessionMemberReq, friendsvc::GetChatSessionMemberRsp>(
                req, rsp, true,
                [this](const friendsvc::GetChatSessionMemberReq& req, friendsvc::GetChatSessionMemberRsp& rsp) {
                    call_friend_service("GetChatSessionMember", req, rsp);
                });
        });

    // ---------- 消息服务路由 ----------

    // 发送新消息（需鉴权）
    http_server_.Post("/api/message/send",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<transmit::NewMessageReq, transmit::NewMessageRsp>(
                req, rsp, true,
                [this](const transmit::NewMessageReq& req, transmit::NewMessageRsp& rsp) {
                    // 1. 调用消息转发服务获取转发目标
                    transmit::GetTransmitTargetRsp target_rsp;
                    if (call_msg_transmit_service("GetTransmitTarget", req, target_rsp)
                        && target_rsp.success()) {
                        // 2. 通知所有目标用户新消息
                        std::vector<std::string> target_ids(target_rsp.target_id_list().begin(),
                                                            target_rsp.target_id_list().end());
                        push_new_message_notify(target_ids, target_rsp.message());

                        // 3. 构建成功响应
                        rsp.set_request_id(req.request_id());
                        rsp.set_success(true);
                    } else {
                        // 转发失败
                        rsp.set_request_id(req.request_id());
                        rsp.set_success(false);
                        rsp.set_errmsg(target_rsp.errmsg());
                    }
                });
        });

    // 获取指定时间段消息列表（需鉴权）
    http_server_.Post("/api/message/history",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<message::GetHistoryMsgReq, message::GetHistoryMsgRsp>(
                req, rsp, true,
                [this](const message::GetHistoryMsgReq& req, message::GetHistoryMsgRsp& rsp) {
                    call_msg_storage_service("GetHistoryMsg", req, rsp);
                });
        });

    // 获取最近N条消息列表（需鉴权）
    http_server_.Post("/api/message/recent",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<message::GetRecentMsgReq, message::GetRecentMsgRsp>(
                req, rsp, true,
                [this](const message::GetRecentMsgReq& req, message::GetRecentMsgRsp& rsp) {
                    call_msg_storage_service("GetRecentMsg", req, rsp);
                });
        });

    // 搜索关键字历史消息（需鉴权）
    http_server_.Post("/api/message/search",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<message::MsgSearchReq, message::MsgSearchRsp>(
                req, rsp, true,
                [this](const message::MsgSearchReq& req, message::MsgSearchRsp& rsp) {
                    call_msg_storage_service("MsgSearch", req, rsp);
                });
        });

    // ---------- 文件服务路由 ----------

    // 单个文件下载（需鉴权）
    http_server_.Post("/api/file/single/download",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<file::GetSingleFileReq, file::GetSingleFileRsp>(
                req, rsp, true,
                [this](const file::GetSingleFileReq& req, file::GetSingleFileRsp& rsp) {
                    call_file_service("GetSingleFile", req, rsp);
                });
        });

    // 多个文件下载（需鉴权）
    http_server_.Post("/api/file/multi/download",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<file::GetMultiFileReq, file::GetMultiFileRsp>(
                req, rsp, true,
                [this](const file::GetMultiFileReq& req, file::GetMultiFileRsp& rsp) {
                    call_file_service("GetMultiFile", req, rsp);
                });
        });

    // 单个文件上传（需鉴权）
    http_server_.Post("/api/file/single/upload",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<file::PutSingleFileReq, file::PutSingleFileRsp>(
                req, rsp, true,
                [this](const file::PutSingleFileReq& req, file::PutSingleFileRsp& rsp) {
                    call_file_service("PutSingleFile", req, rsp);
                });
        });

    // 多个文件上传（需鉴权）
    http_server_.Post("/api/file/multi/upload",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<file::PutMultiFileReq, file::PutMultiFileRsp>(
                req, rsp, true,
                [this](const file::PutMultiFileReq& req, file::PutMultiFileRsp& rsp) {
                    call_file_service("PutMultiFile", req, rsp);
                });
        });

    // ---------- 语音服务路由 ----------

    // 语音转文字（需鉴权）
    http_server_.Post("/api/speech/recognize",
        [this](const httplib::Request& req, httplib::Response& rsp) {
            handle_rpc_request<speech::SpeechRecognitionReq, speech::SpeechRecognitionRsp>(
                req, rsp, true,
                [this](const speech::SpeechRecognitionReq& req, speech::SpeechRecognitionRsp& rsp) {
                    call_speech_service("SpeechRecognition", req, rsp);
                });
        });

    LOG_INFO("[GatewayServerImpl] Initialized with {} HTTP routes", http_port_);
}

// =============================================================================
// 启动与停止
// =============================================================================

bool GatewayServerImpl::start() {
    if (running_) {
        LOG_WARN("[GatewayServerImpl] Already running");
        return true;
    }

    running_ = true;

    // 启动WebSocket服务器在独立线程中
    ws_thread_ = std::thread([this]() {
        try {
            ws_server_.init_asio();
            ws_server_.start_accept();
            LOG_INFO("[GatewayServerImpl] WebSocket server started on port {}", ws_port_);
            ws_server_.run();
        } catch (const std::exception& e) {
            LOG_ERROR("[GatewayServerImpl] WebSocket server error: {}", e.what());
        }
    });

    // 初始化连接管理器的WebSocket服务器指针
    conn_manager_.init(&ws_server_);

    // 启动HTTP服务器（阻塞运行）
    try {
        LOG_INFO("[GatewayServerImpl] HTTP server starting on port {}", http_port_);
        http_server_.listen("0.0.0.0", http_port_);
    } catch (const std::exception& e) {
        LOG_ERROR("[GatewayServerImpl] HTTP server error: {}", e.what());
        running_ = false;
        return false;
    }

    return true;
}

void GatewayServerImpl::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 停止HTTP服务器
    http_server_.stop();
    LOG_INFO("[GatewayServerImpl] HTTP server stopped");

    // 停止WebSocket服务器
    ws_server_.stop();
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    LOG_INFO("[GatewayServerImpl] WebSocket server stopped");
}

} // namespace gateway
