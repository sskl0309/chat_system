// =============================================================================
// user_service_impl.cc - 用户服务 RPC 接口实现
// =============================================================================
// 本文件实现 user_service_impl.hpp 中声明的 11 个 RPC 接口。
//
// 实现要点：
//   - 所有 RPC 方法使用 brpc::ClosureGuard 确保 done->Run() 一定被调用
//   - 参数校验：检查关键字段格式与合法性
//   - 错误处理：捕获异常并记录日志，向客户端返回友好的错误信息
//   - 日志记录：记录关键操作日志，便于排查问题
// =============================================================================

#include "user_service_impl.hpp"
#include "file.pb.h"
#include "log.hpp"
#include "utils.hpp"

#include <brpc/controller.h>
#include <regex>
#include <fstream>

namespace chat {

// =============================================================================
// 构造与析构函数
// =============================================================================

UserServiceImpl::UserServiceImpl() {}

UserServiceImpl::~UserServiceImpl() {}

void UserServiceImpl::set_user_table(std::shared_ptr<user_table::UserTable> user_table) {
    user_table_ = user_table;
}

void UserServiceImpl::set_redis_client(std::shared_ptr<redis_client::RedisClient> redis_client) {
    redis_client_ = redis_client;
}

void UserServiceImpl::set_email_client(std::shared_ptr<email_client::EmailClient> email_client) {
    email_client_ = email_client;
}

void UserServiceImpl::set_user_es(std::shared_ptr<user_es::UserES> user_es) {
    user_es_ = user_es;
}

void UserServiceImpl::set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool) {
    channel_pool_ = channel_pool;
}

// =============================================================================
// 私有辅助方法
// =============================================================================

// 从文件服务获取头像数据
// 流程：
//   1. 校验 RPC 信道池是否就绪
//   2. 通过信道池获取 file_service 的信道（信道池基于 etcd 动态维护）
//   3. 构造 GetSingleFile 请求，通过 brpc stub 同步调用文件服务
//   4. 检查 RPC 调用是否出错（网络故障 / 超时 / 服务端返回错误）
//   5. 提取响应中的文件二进制数据
// 边界情况：
//   - 信道池未初始化 → 返回 false（需调用方确保已注入 channel_pool）
//   - 信道获取失败 → 文件服务可能已下线，返回 false
//   - RPC 调用失败 → 记录详细错误（网络错误文本 或 服务端 errmsg）
//   - 响应中无 file_data → 文件ID可能无效，返回 false
bool UserServiceImpl::get_avatar_from_file_service(const std::string& avatar_id,
                                                    std::string& avatar_data) {
    // 1. 校验信道池是否就绪
    if (!channel_pool_) {
        LOG_ERROR("[UserServiceImpl] Channel pool not initialized");
        return false;
    }

    // 2. 通过 etcd 服务发现获取 file_service 的信道
    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) {
        LOG_ERROR("[UserServiceImpl] No file_service channel available");
        return false;
    }

    // 3. 构造 GetSingleFile 请求，通过 brpc stub 同步调用文件服务
    file::FileService_Stub stub(channel.get());
    file::GetSingleFileReq req;
    file::GetSingleFileRsp rsp;

    req.set_request_id(utils::generate_uuid());
    req.set_file_id(avatar_id);

    // 4. 同步调用（阻塞等待响应）
    brpc::Controller cntl;
    stub.GetSingleFile(&cntl, &req, &rsp, nullptr);

    // 5. 检查调用结果
    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[UserServiceImpl] Get avatar from file service failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    // 6. 提取文件数据
    if (rsp.has_file_data()) {
        avatar_data = rsp.file_data().file_content();
        return true;
    }
    return false;
}

// 上传头像到文件服务
// 流程：
//   1. 校验 RPC 信道池是否就绪
//   2. 获取 file_service 的信道
//   3. 构造 PutSingleFile 请求，将头像二进制数据封装在 FileUploadData 中
//   4. 文件名格式：{user_id}_avatar（便于文件服务端按用户索引）
//   5. 同步调用文件服务的 PutSingleFile RPC
//   6. 从响应中提取文件服务返回的 file_id，用于后续数据库存储
// 边界情况：
//   - 信道池未初始化 → 返回 false
//   - 文件服务不可用 → 返回 false（记录详细错误）
//   - 响应中无 file_info → 上传成功但返回数据异常，返回 false
// 设计考量：
//   - 文件实际内容存储在文件服务，本服务仅保存 file_id（引用）
//   - 头像数据直接通过 protobuf message 携带（非流式），适合小文件场景
bool UserServiceImpl::upload_avatar_to_file_service(const std::string& user_id,
                                                     const std::string& avatar_data,
                                                     std::string& avatar_id) {
    // 1. 校验信道池是否就绪
    if (!channel_pool_) {
        LOG_ERROR("[UserServiceImpl] Channel pool not initialized");
        return false;
    }

    // 2. 获取 file_service 的信道
    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) {
        LOG_ERROR("[UserServiceImpl] No file_service channel available");
        return false;
    }

    // 3. 构造 PutSingleFile 请求，封装头像数据
    file::FileService_Stub stub(channel.get());
    file::PutSingleFileReq req;
    file::PutSingleFileRsp rsp;

    req.set_request_id(utils::generate_uuid());
    req.set_user_id(user_id);

    file::FileUploadData* upload_data = req.mutable_file_data();
    upload_data->set_file_name(user_id + "_avatar");
    upload_data->set_file_size(avatar_data.size());
    upload_data->set_file_content(avatar_data);

    // 4. 同步调用文件服务
    brpc::Controller cntl;
    stub.PutSingleFile(&cntl, &req, &rsp, nullptr);

    // 5. 检查调用结果
    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[UserServiceImpl] Upload avatar to file service failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    // 6. 提取文件服务返回的 file_id
    if (rsp.has_file_info()) {
        avatar_id = rsp.file_info().file_id();
        return true;
    }
    return false;
}

// 将数据库用户信息同步到 Elasticsearch 搜索索引
// 在用户信息（昵称/邮箱/签名/头像）发生变更时调用，保持 ES 与数据库一致。
// 流程：
//   1. 检查 ES 客户端和用户对象是否有效
//   2. 将 ODB 持久化对象转换为 ESUser 结构体（仅复制搜索所需字段）
//   3. 调用 ES insert_data（ES 中相同 _id 即为覆盖更新）
// 设计考量：
//   - 采用"尽力而为"策略：ES 同步失败不影响主业务流程（数据库已更新成功）
//   - ES 主要用于搜索，暂时不一致可接受（下次用户信息变更会重新同步）
//   - 昵称/邮箱/签名/头像 ID 使用 odb optional，需判空后访问
bool UserServiceImpl::sync_user_to_es(const user_table::UserTable::UserPtr& u) {
    // 1. 检查 ES 客户端和用户对象有效性
    if (!user_es_ || !u) {
        return false;
    }

    // 2. 将 ODB 对象转换为 ESUser 结构体
    user_es::ESUser es_user;
    es_user.user_id = u->user_id();
    if (u->nickname()) es_user.nickname = *u->nickname();
    if (u->email()) es_user.email = *u->email();
    if (u->description()) es_user.description = *u->description();
    if (u->avatar_id()) es_user.avatar_id = *u->avatar_id();

    // 3. 写入 ES（相同 _id 为覆盖更新）
    return user_es_->insert_data(es_user);
}

// =============================================================================
// 用户名注册 RPC 接口实现
// =============================================================================

void UserServiceImpl::UserRegister(google::protobuf::RpcController* cntl_base,
                                    const UserRegisterReq* request,
                                    UserRegisterRsp* response,
                                    google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 从请求中取出昵称和密码
    const std::string& nickname = request->nickname();
    const std::string& password = request->password();

    // 2. 检查昵称是否合法（字母、数字、-.、下划线_，长度3~15）
    std::regex nickname_regex("^[a-zA-Z0-9_.\\-]{3,15}$");
    if (!std::regex_match(nickname, nickname_regex)) {
        LOG_WARN("[UserServiceImpl] Invalid nickname format: {}", nickname);
        response->set_errmsg("Invalid nickname format");
        return;
    }

    // 3. 检查密码是否合法（字母、数字，长度6~15）
    std::regex password_regex("^[a-zA-Z0-9]{6,15}$");
    if (!std::regex_match(password, password_regex)) {
        LOG_WARN("[UserServiceImpl] Invalid password format");
        response->set_errmsg("Invalid password format");
        return;
    }

    // 4. 判断昵称是否已存在
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto exist_user = user_table_->select_by_nickname(nickname);
    if (exist_user) {
        LOG_WARN("[UserServiceImpl] Nickname already exists: {}", nickname);
        response->set_errmsg("Nickname already exists");
        return;
    }

    // 5. 向数据库新增数据
    std::string user_id = "USER" + utils::generate_uuid();
    user new_user(user_id, nickname, password);
    if (!user_table_->insert(new_user)) {
        response->set_errmsg("Failed to insert user");
        return;
    }

    // 6. 向 ES 服务器中新增用户信息
    if (user_es_) {
        user_es::ESUser es_user;
        es_user.user_id = user_id;
        es_user.nickname = nickname;
        user_es_->insert_data(es_user);
    }

    // 7. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] User register success, user_id: {}, nickname: {}", user_id, nickname);
}

// =============================================================================
// 用户名登录 RPC 接口实现
// =============================================================================

void UserServiceImpl::UserLogin(google::protobuf::RpcController* cntl_base,
                                 const UserLoginReq* request,
                                 UserLoginRsp* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 从请求中取出昵称和密码
    const std::string& nickname = request->nickname();
    const std::string& password = request->password();

    // 2. 通过昵称获取用户信息，进行密码验证
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto u = user_table_->select_by_nickname(nickname);
    if (!u) {
        LOG_WARN("[UserServiceImpl] User not found: {}", nickname);
        response->set_errmsg("User not found");
        return;
    }

    // 密码验证
    if (!u->passwd() || *u->passwd() != password) {
        LOG_WARN("[UserServiceImpl] Password mismatch, nickname: {}", nickname);
        response->set_errmsg("Password mismatch");
        return;
    }

    // 3. 判断用户是否已登录
    if (redis_client_ && redis_client_->is_user_logged_in(u->user_id())) {
        LOG_WARN("[UserServiceImpl] User already logged in: {}", u->user_id());
        response->set_errmsg("User already logged in");
        return;
    }

    // 4. 构造会话ID，添加到 Redis
    if (!redis_client_) {
        response->set_errmsg("Redis not initialized");
        return;
    }
    std::string session_id = "SID-" + utils::generate_uuid();
    if (!redis_client_->create_session(session_id, u->user_id())) {
        response->set_errmsg("Failed to create session");
        return;
    }

    // 5. 组织响应
    response->set_success(true);
    response->set_login_session_id(session_id);
    LOG_INFO("[UserServiceImpl] User login success, user_id: {}, nickname: {}", u->user_id(), nickname);
}

// =============================================================================
// 获取邮箱验证码 RPC 接口实现
// =============================================================================

void UserServiceImpl::GetEmailVerifyCode(google::protobuf::RpcController* cntl_base,
                                          const EmailVerifyCodeReq* request,
                                          EmailVerifyCodeRsp* response,
                                          google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 从请求中取出邮箱地址
    const std::string& email = request->email();

    // 2. 验证邮箱格式
    std::regex email_regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
    if (!std::regex_match(email, email_regex)) {
        LOG_WARN("[UserServiceImpl] Invalid email format: {}", email);
        response->set_errmsg("Invalid email format");
        return;
    }

    // 3. 生成 4 位随机验证码
    std::string verify_code = email_client::EmailClient::generate_code(4);

    // 4. 发送验证码邮件
    if (!email_client_) {
        response->set_errmsg("Email client not initialized");
        return;
    }
    if (!email_client_->send_verify_code(email, verify_code)) {
        response->set_errmsg("Failed to send verify code email");
        return;
    }

    // 5. 构造验证码ID，添加到 Redis
    if (!redis_client_) {
        response->set_errmsg("Redis not initialized");
        return;
    }
    std::string code_id = "VID-" + utils::generate_uuid();
    if (!redis_client_->set_verify_code(code_id, verify_code, 300)) {
        response->set_errmsg("Failed to store verify code");
        return;
    }

    // 6. 组织响应
    response->set_success(true);
    response->set_verify_code_id(code_id);
    LOG_INFO("[UserServiceImpl] Email verify code sent, email: {}", email);
}

// =============================================================================
// 邮箱注册 RPC 接口实现
// =============================================================================

void UserServiceImpl::EmailRegister(google::protobuf::RpcController* cntl_base,
                                     const EmailRegisterReq* request,
                                     EmailRegisterRsp* response,
                                     google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 从请求中取出邮箱和验证码
    const std::string& email = request->email();
    const std::string& code_id = request->verify_code_id();
    const std::string& code = request->verify_code();

    // 2. 检查邮箱格式
    std::regex email_regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
    if (!std::regex_match(email, email_regex)) {
        response->set_errmsg("Invalid email format");
        return;
    }

    // 3. 从 Redis 进行验证码匹配
    if (!redis_client_) {
        response->set_errmsg("Redis not initialized");
        return;
    }
    if (!redis_client_->verify_code(code_id, code)) {
        response->set_errmsg("Invalid verify code");
        return;
    }

    // 4. 判断邮箱是否已注册
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto exist_user = user_table_->select_by_email(email);
    if (exist_user) {
        LOG_WARN("[UserServiceImpl] Email already registered: {}", email);
        response->set_errmsg("Email already registered");
        return;
    }

    // 5. 向数据库新增用户
    std::string user_id = "USER" + utils::generate_uuid();
    user new_user(user_id);
    new_user.email(email);
    if (!user_table_->insert(new_user)) {
        response->set_errmsg("Failed to insert user");
        return;
    }

    // 6. 向 ES 新增用户信息
    if (user_es_) {
        user_es::ESUser es_user;
        es_user.user_id = user_id;
        es_user.email = email;
        user_es_->insert_data(es_user);
    }

    // 7. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] Email register success, user_id: {}, email: {}", user_id, email);
}

// =============================================================================
// 邮箱登录 RPC 接口实现
// =============================================================================

void UserServiceImpl::EmailLogin(google::protobuf::RpcController* cntl_base,
                                  const EmailLoginReq* request,
                                  EmailLoginRsp* response,
                                  google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 从请求中取出邮箱、验证码ID、验证码
    const std::string& email = request->email();
    const std::string& code_id = request->verify_code_id();
    const std::string& code = request->verify_code();

    // 2. 检查邮箱格式
    std::regex email_regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
    if (!std::regex_match(email, email_regex)) {
        response->set_errmsg("Invalid email format");
        return;
    }

    // 3. 验证码匹配
    if (!redis_client_) {
        response->set_errmsg("Redis not initialized");
        return;
    }
    if (!redis_client_->verify_code(code_id, code)) {
        response->set_errmsg("Invalid verify code");
        return;
    }

    // 4. 查询用户信息
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto u = user_table_->select_by_email(email);
    if (!u) {
        LOG_WARN("[UserServiceImpl] Email not found: {}", email);
        response->set_errmsg("Email not registered");
        return;
    }

    // 5. 判断用户是否已登录
    if (redis_client_->is_user_logged_in(u->user_id())) {
        LOG_WARN("[UserServiceImpl] User already logged in: {}", u->user_id());
        response->set_errmsg("User already logged in");
        return;
    }

    // 6. 构造会话ID，添加到 Redis
    std::string session_id = "SID-" + utils::generate_uuid();
    if (!redis_client_->create_session(session_id, u->user_id())) {
        response->set_errmsg("Failed to create session");
        return;
    }

    // 7. 组织响应
    response->set_success(true);
    response->set_login_session_id(session_id);
    LOG_INFO("[UserServiceImpl] Email login success, user_id: {}, email: {}", u->user_id(), email);
}

// =============================================================================
// 获取用户信息 RPC 接口实现
// =============================================================================

void UserServiceImpl::GetUserInfo(google::protobuf::RpcController* cntl_base,
                                   const GetUserInfoReq* request,
                                   GetUserInfoRsp* response,
                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 从请求中取出用户ID
    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    const std::string& user_id = request->user_id();

    // 2. 从数据库查询用户信息
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto u = user_table_->select_by_user_id(user_id);
    if (!u) {
        LOG_WARN("[UserServiceImpl] User not found: {}", user_id);
        response->set_errmsg("User not found");
        return;
    }

    // 3. 从文件服务获取头像数据
    file::UserInfo* user_info = response->mutable_user_info();
    user_info->set_user_id(u->user_id());
    if (u->nickname()) user_info->set_nickname(*u->nickname());
    if (u->description()) user_info->set_description(*u->description());
    if (u->email()) user_info->set_email(*u->email());

    if (u->avatar_id() && !u->avatar_id()->empty()) {
        std::string avatar_data;
        if (get_avatar_from_file_service(*u->avatar_id(), avatar_data)) {
            user_info->set_avatar(avatar_data);
        } else {
            LOG_WARN("[UserServiceImpl] Failed to get avatar, user_id: {}, avatar_id: {}",
                      user_id, *u->avatar_id());
        }
    }

    // 4. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] GetUserInfo success, user_id: {}", user_id);
}

// =============================================================================
// 批量获取用户信息 RPC 接口实现
// =============================================================================

void UserServiceImpl::GetMultiUserInfo(google::protobuf::RpcController* cntl_base,
                                        const GetMultiUserInfoReq* request,
                                        GetMultiUserInfoRsp* response,
                                        google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }

    // 收集用户ID列表
    std::vector<std::string> user_ids;
    for (int i = 0; i < request->users_id_size(); ++i) {
        user_ids.push_back(request->users_id(i));
    }

    // 批量查询
    auto users = user_table_->select_multi(user_ids);

    // 构造响应
    auto* users_info = response->mutable_users_info();
    for (const auto& pair : users) {
        const auto& u = pair.second;
        file::UserInfo info;
        info.set_user_id(u->user_id());
        if (u->nickname()) info.set_nickname(*u->nickname());
        if (u->description()) info.set_description(*u->description());
        if (u->email()) info.set_email(*u->email());

        // 获取头像
        if (u->avatar_id() && !u->avatar_id()->empty()) {
            std::string avatar_data;
            if (get_avatar_from_file_service(*u->avatar_id(), avatar_data)) {
                info.set_avatar(avatar_data);
            }
        }

        (*users_info)[pair.first] = info;
    }

    response->set_success(true);
    LOG_INFO("[UserServiceImpl] GetMultiUserInfo success, count: {}", users.size());
}

// =============================================================================
// 设置用户头像 RPC 接口实现
// =============================================================================

void UserServiceImpl::SetUserAvatar(google::protobuf::RpcController* cntl_base,
                                     const SetUserAvatarReq* request,
                                     SetUserAvatarRsp* response,
                                     google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 取出用户ID和头像数据
    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    const std::string& user_id = request->user_id();

    if (request->avatar().empty()) {
        response->set_errmsg("Empty avatar data");
        return;
    }

    // 2. 查询用户是否存在
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto u = user_table_->select_by_user_id(user_id);
    if (!u) {
        response->set_errmsg("User not found");
        return;
    }

    // 3. 上传头像到文件服务
    std::string avatar_id;
    std::string avatar_data(request->avatar().data(), request->avatar().size());
    if (!upload_avatar_to_file_service(user_id, avatar_data, avatar_id)) {
        response->set_errmsg("Failed to upload avatar");
        return;
    }

    // 4. 更新数据库中的头像ID
    u->avatar_id(avatar_id);
    if (!user_table_->update(*u)) {
        response->set_errmsg("Failed to update user avatar");
        return;
    }

    // 5. 更新 ES
    sync_user_to_es(u);

    // 6. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] SetUserAvatar success, user_id: {}, avatar_id: {}", user_id, avatar_id);
}

// =============================================================================
// 设置用户昵称 RPC 接口实现
// =============================================================================

void UserServiceImpl::SetUserNickname(google::protobuf::RpcController* cntl_base,
                                       const SetUserNicknameReq* request,
                                       SetUserNicknameRsp* response,
                                       google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 取出用户ID和新昵称
    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    const std::string& user_id = request->user_id();
    const std::string& nickname = request->nickname();

    // 2. 判断昵称格式
    std::regex nickname_regex("^[a-zA-Z0-9_.\\-]{3,15}$");
    if (!std::regex_match(nickname, nickname_regex)) {
        response->set_errmsg("Invalid nickname format");
        return;
    }

    // 3. 查询用户是否存在
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto u = user_table_->select_by_user_id(user_id);
    if (!u) {
        response->set_errmsg("User not found");
        return;
    }

    // 4. 更新数据库
    u->nickname(nickname);
    if (!user_table_->update(*u)) {
        response->set_errmsg("Failed to update nickname");
        return;
    }

    // 5. 更新 ES
    sync_user_to_es(u);

    // 6. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] SetUserNickname success, user_id: {}, nickname: {}", user_id, nickname);
}

// =============================================================================
// 设置用户签名 RPC 接口实现
// =============================================================================

void UserServiceImpl::SetUserDescription(google::protobuf::RpcController* cntl_base,
                                          const SetUserDescriptionReq* request,
                                          SetUserDescriptionRsp* response,
                                          google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 取出用户ID和新签名
    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    const std::string& user_id = request->user_id();
    const std::string& description = request->description();

    // 2. 查询用户是否存在
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto u = user_table_->select_by_user_id(user_id);
    if (!u) {
        response->set_errmsg("User not found");
        return;
    }

    // 3. 更新数据库
    u->description(description);
    if (!user_table_->update(*u)) {
        response->set_errmsg("Failed to update description");
        return;
    }

    // 4. 更新 ES
    sync_user_to_es(u);

    // 5. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] SetUserDescription success, user_id: {}", user_id);
}

// =============================================================================
// 设置用户邮箱 RPC 接口实现
// =============================================================================

void UserServiceImpl::SetUserEmail(google::protobuf::RpcController* cntl_base,
                                    const SetUserEmailReq* request,
                                    SetUserEmailRsp* response,
                                    google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 1. 取出用户ID、邮箱、验证码
    if (!request->has_user_id() || request->user_id().empty()) {
        response->set_errmsg("Missing user_id");
        return;
    }
    const std::string& user_id = request->user_id();
    const std::string& email = request->email();
    const std::string& code_id = request->email_verify_code_id();
    const std::string& code = request->email_verify_code();

    // 2. 检查邮箱格式
    std::regex email_regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
    if (!std::regex_match(email, email_regex)) {
        response->set_errmsg("Invalid email format");
        return;
    }

    // 3. 验证码匹配
    if (!redis_client_) {
        response->set_errmsg("Redis not initialized");
        return;
    }
    if (!redis_client_->verify_code(code_id, code)) {
        response->set_errmsg("Invalid verify code");
        return;
    }

    // 4. 判断邮箱是否已被其他用户绑定
    if (!user_table_) {
        response->set_errmsg("Database not initialized");
        return;
    }
    auto exist_user = user_table_->select_by_email(email);
    if (exist_user && exist_user->user_id() != user_id) {
        response->set_errmsg("Email already bound by another user");
        return;
    }

    // 5. 查询用户并更新邮箱
    auto u = user_table_->select_by_user_id(user_id);
    if (!u) {
        response->set_errmsg("User not found");
        return;
    }

    u->email(email);
    if (!user_table_->update(*u)) {
        response->set_errmsg("Failed to update email");
        return;
    }

    // 6. 更新 ES
    sync_user_to_es(u);

    // 7. 组织响应
    response->set_success(true);
    LOG_INFO("[UserServiceImpl] SetUserEmail success, user_id: {}, email: {}", user_id, email);
}

} // namespace chat
