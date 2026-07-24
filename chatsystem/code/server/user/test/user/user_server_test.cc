// =============================================================================
// user_server_test.cc - 用户服务端到端测试客户端
// =============================================================================
// 按照测试要求，对 UserService 的全部 11 个 RPC 接口进行端到端测试：
//
// 测试顺序：
//   1. 用户注册（用户名+密码）
//   2. 用户登录（获取 session_id）
//   3. 设置头像
//   4. 设置签名
//   5. 设置昵称
//   6. 获取用户信息（与已有信息比对）
//   7. 多注册几个用户，批量获取用户信息测试
//   8. 获取邮箱验证码 → 邮箱注册 → 邮箱登录 → 设置用户邮箱
//
// 测试数据结构：
//   - 用户信息：用户名、用户ID、邮箱、签名、头像
//   - 密码对象：密码信息
//   - 头像ID对象：存储头像文件ID
//   - 登录会话ID：登录成功后返回的 session_id
//
// 运行方式：
//   ./user_server_test --user_server_addr=127.0.0.1:10001
//
// 注意：需先启动 user_server，依赖 Redis、MySQL、ES 服务
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <random>
#include <sstream>

#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "user.pb.h"
#include "log.hpp"

// ==================== gflags 命令行参数 ====================

DEFINE_string(user_server_addr, "127.0.0.1:10001", "User server address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");

// ==================== 测试数据结构 ====================

/**
 * @brief 用户测试数据结构
 */
struct TestUser {
    std::string nickname;      // 用户昵称
    std::string password;      // 密码（MD5 加密）
    std::string user_id;       // 用户ID（注册后获取）
    std::string email;         // 用户邮箱
    std::string description;   // 用户签名
    std::string avatar_id;     // 头像文件ID（设置头像后获取）
    std::string session_id;    // 登录会话ID（登录后获取）
};

// ==================== 工具函数 ====================

/**
 * @brief 生成随机字符串（用于创建唯一测试数据）
 */
static std::string random_string(int length = 8) {
    const std::string charset = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, charset.size() - 1);
    
    std::string result;
    for (int i = 0; i < length; ++i) {
        result += charset[dist(gen)];
    }
    return result;
}

/**
 * @brief 生成唯一请求ID（用于链路追踪）
 */
static std::string generate_request_id() {
    return "req_" + random_string(16);
}

/**
 * @brief 生成简单的头像二进制数据（1x1 像素的 PNG）
 */
static std::string generate_test_avatar() {
    // 最小有效 PNG 文件（1x1 红色像素）
    const unsigned char png_data[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0A, 0x49, 0x44, 0x41, 0x54, 0x48, 0xC7, 0xED, 0xC1, 0x01, 0x01, 0x00,
        0x00, 0x00, 0x82, 0x20, 0xF8, 0x4F, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x5E, 0x32, 0x0E, 0x15, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82
    };
    return std::string(reinterpret_cast<const char*>(png_data), sizeof(png_data));
}

/**
 * @brief 打印测试结果
 */
static void print_test_result(const std::string& test_name, bool success, 
                              const std::string& detail = "") {
    if (success) {
        std::cout << "[PASS] " << test_name << std::endl;
    } else {
        std::cout << "[FAIL] " << test_name;
        if (!detail.empty()) {
            std::cout << " - " << detail;
        }
        std::cout << std::endl;
    }
}

/**
 * @brief 打印用户信息
 */
static void print_user_info(const TestUser& user) {
    std::cout << "  ├─ nickname: " << user.nickname << std::endl;
    std::cout << "  ├─ password: " << user.password << std::endl;
    std::cout << "  ├─ user_id: " << user.user_id << std::endl;
    std::cout << "  ├─ email: " << user.email << std::endl;
    std::cout << "  ├─ description: " << user.description << std::endl;
    std::cout << "  ├─ avatar_id: " << user.avatar_id << std::endl;
    std::cout << "  └─ session_id: " << user.session_id << std::endl;
}

// ==================== 测试函数声明 ====================

bool test_user_register(chat::UserService_Stub& stub, TestUser& user);
bool test_user_login(chat::UserService_Stub& stub, TestUser& user);
bool test_set_user_avatar(chat::UserService_Stub& stub, TestUser& user);
bool test_set_user_description(chat::UserService_Stub& stub, TestUser& user);
bool test_set_user_nickname(chat::UserService_Stub& stub, TestUser& user);
bool test_get_user_info(chat::UserService_Stub& stub, const TestUser& user);
bool test_get_multi_user_info(chat::UserService_Stub& stub, 
                              const std::vector<TestUser>& users);
bool test_get_email_verify_code(chat::UserService_Stub& stub, 
                                const std::string& email, std::string& code_id);
bool test_email_register(chat::UserService_Stub& stub, 
                         const std::string& email, const std::string& code_id,
                         const std::string& code);
bool test_email_login(chat::UserService_Stub& stub,
                      const std::string& email, const std::string& code_id,
                      const std::string& code, TestUser& user);
bool test_set_user_email(chat::UserService_Stub& stub, TestUser& user,
                         const std::string& new_email, const std::string& code_id,
                         const std::string& code);

// ==================== 测试函数实现 ====================

// ---------------------------------------------------------------------------
// 测试 1：用户注册
// ---------------------------------------------------------------------------
bool test_user_register(chat::UserService_Stub& stub, TestUser& user) {
    std::cout << "\n=== 测试 1：用户注册 ===" << std::endl;
    
    chat::UserRegisterReq req;
    chat::UserRegisterRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_nickname(user.nickname);
    req.set_password(user.password);
    
    stub.UserRegister(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("UserRegister", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("UserRegister", false, rsp.errmsg());
        return false;
    }
    
    print_test_result("UserRegister", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 2：用户登录
// ---------------------------------------------------------------------------
bool test_user_login(chat::UserService_Stub& stub, TestUser& user) {
    std::cout << "\n=== 测试 2：用户登录 ===" << std::endl;
    
    chat::UserLoginReq req;
    chat::UserLoginRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_nickname(user.nickname);
    req.set_password(user.password);
    
    stub.UserLogin(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("UserLogin", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("UserLogin", false, rsp.errmsg());
        return false;
    }
    
    user.session_id = rsp.login_session_id();
    user.user_id = rsp.user_id();
    std::cout << "  获取到 session_id: " << user.session_id << std::endl;
    std::cout << "  获取到 user_id: " << user.user_id << std::endl;
    
    print_test_result("UserLogin", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 3：设置用户头像
// ---------------------------------------------------------------------------
bool test_set_user_avatar(chat::UserService_Stub& stub, TestUser& user) {
    std::cout << "\n=== 测试 3：设置用户头像 ===" << std::endl;
    
    chat::SetUserAvatarReq req;
    chat::SetUserAvatarRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_user_id(user.user_id);
    req.set_session_id(user.session_id);
    
    std::string avatar_data = generate_test_avatar();
    req.set_avatar(avatar_data);
    
    stub.SetUserAvatar(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("SetUserAvatar", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("SetUserAvatar", false, rsp.errmsg());
        return false;
    }
    
    user.avatar_id = "avatar_" + random_string(10);  // 模拟返回的 avatar_id
    print_test_result("SetUserAvatar", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 4：设置用户签名
// ---------------------------------------------------------------------------
bool test_set_user_description(chat::UserService_Stub& stub, TestUser& user) {
    std::cout << "\n=== 测试 4：设置用户签名 ===" << std::endl;
    
    chat::SetUserDescriptionReq req;
    chat::SetUserDescriptionRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_user_id(user.user_id);
    req.set_session_id(user.session_id);
    req.set_description(user.description);
    
    stub.SetUserDescription(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("SetUserDescription", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("SetUserDescription", false, rsp.errmsg());
        return false;
    }
    
    print_test_result("SetUserDescription", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 5：设置用户昵称
// ---------------------------------------------------------------------------
bool test_set_user_nickname(chat::UserService_Stub& stub, TestUser& user) {
    std::cout << "\n=== 测试 5：设置用户昵称 ===" << std::endl;
    
    // 新昵称必须符合长度要求（3-15字符，仅字母数字下划线）
    std::string new_nickname = user.nickname.substr(0, 6) + "new";  // 保持在15字符以内
    
    chat::SetUserNicknameReq req;
    chat::SetUserNicknameRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_user_id(user.user_id);
    req.set_session_id(user.session_id);
    req.set_nickname(new_nickname);
    
    stub.SetUserNickname(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("SetUserNickname", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("SetUserNickname", false, rsp.errmsg());
        return false;
    }
    
    user.nickname = new_nickname;  // 更新本地记录
    print_test_result("SetUserNickname", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 6：获取用户信息（与已有信息比对）
// ---------------------------------------------------------------------------
bool test_get_user_info(chat::UserService_Stub& stub, const TestUser& user) {
    std::cout << "\n=== 测试 6：获取用户信息 ===" << std::endl;
    
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_user_id(user.user_id);
    req.set_session_id(user.session_id);
    
    stub.GetUserInfo(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("GetUserInfo", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("GetUserInfo", false, rsp.errmsg());
        return false;
    }
    
    // 打印返回的用户信息
    const auto& info = rsp.user_info();
    std::cout << "  返回的用户信息：" << std::endl;
    std::cout << "    user_id: " << info.user_id() << std::endl;
    std::cout << "    nickname: " << info.nickname() << std::endl;
    std::cout << "    description: " << info.description() << std::endl;
    std::cout << "    email: " << info.email() << std::endl;
    std::cout << "    avatar_size: " << info.avatar().size() << " bytes" << std::endl;
    
    // 验证关键字段
    bool matched = true;
    if (info.user_id() != user.user_id) {
        std::cout << "    [WARN] user_id 不匹配: expected=" << user.user_id 
                  << ", actual=" << info.user_id() << std::endl;
        matched = false;
    }
    if (info.nickname() != user.nickname) {
        std::cout << "    [WARN] nickname 不匹配: expected=" << user.nickname 
                  << ", actual=" << info.nickname() << std::endl;
        matched = false;
    }
    if (info.description() != user.description) {
        std::cout << "    [WARN] description 不匹配: expected=" << user.description 
                  << ", actual=" << info.description() << std::endl;
        matched = false;
    }
    
    print_test_result("GetUserInfo", matched);
    return matched;
}

// ---------------------------------------------------------------------------
// 测试 7：批量获取用户信息
// ---------------------------------------------------------------------------
bool test_get_multi_user_info(chat::UserService_Stub& stub, 
                              const std::vector<TestUser>& users) {
    std::cout << "\n=== 测试 7：批量获取用户信息 ===" << std::endl;
    
    chat::GetMultiUserInfoReq req;
    chat::GetMultiUserInfoRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    for (const auto& user : users) {
        req.add_users_id(user.user_id);
    }
    
    stub.GetMultiUserInfo(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("GetMultiUserInfo", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("GetMultiUserInfo", false, rsp.errmsg());
        return false;
    }
    
    std::cout << "  请求 " << users.size() << " 个用户，返回 " 
              << rsp.users_info_size() << " 条记录" << std::endl;
    
    for (const auto& entry : rsp.users_info()) {
        std::cout << "    user_id: " << entry.first 
                  << ", nickname: " << entry.second.nickname() << std::endl;
    }
    
    print_test_result("GetMultiUserInfo", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 8：获取邮箱验证码
// ---------------------------------------------------------------------------
bool test_get_email_verify_code(chat::UserService_Stub& stub, 
                                const std::string& email, std::string& code_id) {
    std::cout << "\n=== 测试 8：获取邮箱验证码 ===" << std::endl;
    
    chat::EmailVerifyCodeReq req;
    chat::EmailVerifyCodeRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_email(email);
    
    stub.GetEmailVerifyCode(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("GetEmailVerifyCode", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("GetEmailVerifyCode", false, rsp.errmsg());
        return false;
    }
    
    code_id = rsp.verify_code_id();
    std::cout << "  获取到 code_id: " << code_id << std::endl;
    std::cout << "  请查看邮箱 " << email << " 获取验证码" << std::endl;
    
    print_test_result("GetEmailVerifyCode", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 9：邮箱注册
// ---------------------------------------------------------------------------
bool test_email_register(chat::UserService_Stub& stub,
                         const std::string& email, const std::string& code_id,
                         const std::string& code) {
    std::cout << "\n=== 测试 9：邮箱注册 ===" << std::endl;
    
    chat::EmailRegisterReq req;
    chat::EmailRegisterRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_email(email);
    req.set_verify_code_id(code_id);
    req.set_verify_code(code);
    
    stub.EmailRegister(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("EmailRegister", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("EmailRegister", false, rsp.errmsg());
        return false;
    }
    
    print_test_result("EmailRegister", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 10：邮箱登录
// ---------------------------------------------------------------------------
bool test_email_login(chat::UserService_Stub& stub,
                      const std::string& email, const std::string& code_id,
                      const std::string& code, TestUser& user) {
    std::cout << "\n=== 测试 10：邮箱登录 ===" << std::endl;
    
    chat::EmailLoginReq req;
    chat::EmailLoginRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_email(email);
    req.set_verify_code_id(code_id);
    req.set_verify_code(code);
    
    stub.EmailLogin(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("EmailLogin", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("EmailLogin", false, rsp.errmsg());
        return false;
    }
    
    user.session_id = rsp.login_session_id();
    user.user_id = rsp.user_id();
    user.email = email;
    std::cout << "  获取到 session_id: " << user.session_id << std::endl;
    std::cout << "  获取到 user_id: " << user.user_id << std::endl;
    
    print_test_result("EmailLogin", true);
    return true;
}

// ---------------------------------------------------------------------------
// 测试 11：设置用户邮箱
// ---------------------------------------------------------------------------
bool test_set_user_email(chat::UserService_Stub& stub, TestUser& user,
                         const std::string& new_email, const std::string& code_id,
                         const std::string& code) {
    std::cout << "\n=== 测试 11：设置用户邮箱 ===" << std::endl;
    
    chat::SetUserEmailReq req;
    chat::SetUserEmailRsp rsp;
    brpc::Controller cntl;
    
    req.set_request_id(generate_request_id());
    req.set_user_id(user.user_id);
    req.set_session_id(user.session_id);
    req.set_email(new_email);
    req.set_email_verify_code_id(code_id);
    req.set_email_verify_code(code);
    
    stub.SetUserEmail(&cntl, &req, &rsp, nullptr);
    
    if (cntl.Failed()) {
        print_test_result("SetUserEmail", false, cntl.ErrorText());
        return false;
    }
    
    if (!rsp.success()) {
        print_test_result("SetUserEmail", false, rsp.errmsg());
        return false;
    }
    
    user.email = new_email;
    print_test_result("SetUserEmail", true);
    return true;
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    // 初始化日志
    mylog::init(true, "", mylog::LogLevel::INFO);
    
    // 创建 brpc channel 连接用户服务
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.timeout_ms = FLAGS_timeout_ms;
    
    if (channel.Init(FLAGS_user_server_addr.c_str(), &options) != 0) {
        std::cerr << "Failed to initialize channel to " << FLAGS_user_server_addr << std::endl;
        return -1;
    }
    
    chat::UserService_Stub stub(&channel);
    std::cout << "Connected to user server: " << FLAGS_user_server_addr << std::endl;
    
    // ==================== 创建测试用户数据 ====================
    std::vector<TestUser> test_users;
    
    // 用户1：用于完整流程测试（昵称4-12字符，密码6-12字符，仅字母数字）
    TestUser user1;
    user1.nickname = "u_" + random_string(6);  // 8字符，符合3-15限制
    user1.password = "pass" + random_string(6); // 10字符，符合6-15限制
    user1.email = user1.nickname + "@test.com";
    user1.description = "test signature";
    
    // 用户2：用于批量获取测试
    TestUser user2;
    user2.nickname = "u_" + random_string(6);
    user2.password = "pass" + random_string(6);
    user2.email = user2.nickname + "@test.com";
    user2.description = "test sig2";
    
    // 用户3：用于批量获取测试
    TestUser user3;
    user3.nickname = "u_" + random_string(6);
    user3.password = "pass" + random_string(6);
    user3.email = user3.nickname + "@test.com";
    user3.description = "test sig3";
    
    // ==================== 执行测试 ====================
    int passed = 0;
    int total = 0;
    
    // 阶段一：用户注册与登录
    total++; passed += test_user_register(stub, user1) ? 1 : 0;
    total++; passed += test_user_login(stub, user1) ? 1 : 0;
    
    // 阶段二：设置头像、签名、昵称
    total++; passed += test_set_user_avatar(stub, user1) ? 1 : 0;
    total++; passed += test_set_user_description(stub, user1) ? 1 : 0;
    total++; passed += test_set_user_nickname(stub, user1) ? 1 : 0;
    
    // 阶段三：获取用户信息（比对）
    total++; passed += test_get_user_info(stub, user1) ? 1 : 0;
    
    // 阶段四：多用户注册与批量获取
    total++; passed += test_user_register(stub, user2) ? 1 : 0;
    total++; passed += test_user_register(stub, user3) ? 1 : 0;
    
    // 登录获取 user_id（用于批量查询）
    test_user_login(stub, user2);
    test_user_login(stub, user3);
    
    total++; passed += test_get_multi_user_info(stub, {user1, user2, user3}) ? 1 : 0;
    
    // 阶段五：邮箱相关测试
    std::string email_code_id;
    std::string test_email = "3049075877@qq.com";  // 使用已配置的邮箱
    
    // 获取邮箱验证码（需要用户手动输入）
    bool code_ok = test_get_email_verify_code(stub, test_email, email_code_id);
    total++; passed += code_ok ? 1 : 0;
    
    if (code_ok) {
        std::string verify_code;
        std::cout << "  请输入收到的验证码: ";
        std::cin >> verify_code;
        std::cout << std::endl;
        
        // 邮箱注册（测试用）
        total++; passed += test_email_register(stub, test_email, email_code_id, verify_code) ? 1 : 0;
        
        // 获取新验证码用于邮箱登录
        std::string login_code_id;
        test_get_email_verify_code(stub, test_email, login_code_id);
        
        std::cout << "  请输入新的验证码(用于登录): ";
        std::cin >> verify_code;
        std::cout << std::endl;
        
        TestUser email_user;
        total++; passed += test_email_login(stub, test_email, login_code_id, verify_code, email_user) ? 1 : 0;
        
        // 设置用户邮箱（需要先登录）
        std::string new_email_code_id;
        std::string new_email = "test_new_" + random_string(6) + "@qq.com";
        test_get_email_verify_code(stub, new_email, new_email_code_id);
        
        std::cout << "  请输入新邮箱 " << new_email << " 的验证码: ";
        std::cin >> verify_code;
        std::cout << std::endl;
        
        total++; passed += test_set_user_email(stub, email_user, new_email, new_email_code_id, verify_code) ? 1 : 0;
    }
    
    // ==================== 测试总结 ====================
    std::cout << "\n" << "=" << std::string(60, '=') << std::endl;
    std::cout << "测试完成！" << std::endl;
    std::cout << "通过: " << passed << " / " << total << std::endl;
    std::cout << "通过率: " << (passed * 100.0 / total) << "%" << std::endl;
    std::cout << "=" << std::string(60, '=') << std::endl;
    
    return passed == total ? 0 : -1;
}
