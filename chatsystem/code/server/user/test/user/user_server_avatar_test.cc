// =============================================================================
// user_server_avatar_test.cc - 用户服务头像上传测试
// =============================================================================
// 专门测试头像上传功能（需要文件服务支持）
// =============================================================================

#include <iostream>
#include <string>
#include <memory>
#include <random>

#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "user.pb.h"
#include "log.hpp"

DEFINE_string(user_server_addr, "127.0.0.1:10001", "User server address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");

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

static std::string generate_request_id() {
    return "req_" + random_string(16);
}

static std::string generate_test_avatar() {
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

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    mylog::init(true, "", mylog::LogLevel::INFO);
    
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.timeout_ms = FLAGS_timeout_ms;
    
    if (channel.Init(FLAGS_user_server_addr.c_str(), &options) != 0) {
        std::cerr << "Failed to initialize channel" << std::endl;
        return -1;
    }
    
    chat::UserService_Stub stub(&channel);
    std::cout << "Connected to user server: " << FLAGS_user_server_addr << std::endl;
    
    // 1. 注册用户
    std::cout << "\n[1] 注册用户..." << std::endl;
    std::string nickname = "av_" + random_string(6);  // 8字符，符合3-15限制
    std::string password = "pass" + random_string(6);
    
    chat::UserRegisterReq reg_req;
    chat::UserRegisterRsp reg_rsp;
    brpc::Controller cntl1;
    
    reg_req.set_request_id(generate_request_id());
    reg_req.set_nickname(nickname);
    reg_req.set_password(password);
    
    stub.UserRegister(&cntl1, &reg_req, &reg_rsp, nullptr);
    if (!reg_rsp.success()) {
        std::cout << "FAIL: " << reg_rsp.errmsg() << std::endl;
        return -1;
    }
    std::cout << "PASS: 用户注册成功" << std::endl;
    
    // 2. 登录用户
    std::cout << "\n[2] 登录用户..." << std::endl;
    chat::UserLoginReq login_req;
    chat::UserLoginRsp login_rsp;
    brpc::Controller cntl2;
    
    login_req.set_request_id(generate_request_id());
    login_req.set_nickname(nickname);
    login_req.set_password(password);
    
    stub.UserLogin(&cntl2, &login_req, &login_rsp, nullptr);
    if (!login_rsp.success()) {
        std::cout << "FAIL: " << login_rsp.errmsg() << std::endl;
        return -1;
    }
    
    std::string user_id = login_rsp.user_id();
    std::string session_id = login_rsp.login_session_id();
    std::cout << "PASS: 用户登录成功" << std::endl;
    std::cout << "  user_id: " << user_id << std::endl;
    std::cout << "  session_id: " << session_id << std::endl;
    
    // 3. 设置头像（关键测试）
    std::cout << "\n[3] 设置用户头像..." << std::endl;
    chat::SetUserAvatarReq avatar_req;
    chat::SetUserAvatarRsp avatar_rsp;
    brpc::Controller cntl3;
    
    avatar_req.set_request_id(generate_request_id());
    avatar_req.set_user_id(user_id);
    avatar_req.set_session_id(session_id);
    
    std::string avatar_data = generate_test_avatar();
    avatar_req.set_avatar(avatar_data);
    
    stub.SetUserAvatar(&cntl3, &avatar_req, &avatar_rsp, nullptr);
    
    if (cntl3.Failed()) {
        std::cout << "FAIL: " << cntl3.ErrorText() << std::endl;
        return -1;
    }
    
    if (!avatar_rsp.success()) {
        std::cout << "FAIL: " << avatar_rsp.errmsg() << std::endl;
        return -1;
    }
    
    std::cout << "PASS: 头像上传成功！" << std::endl;
    
    // 4. 获取用户信息验证头像
    std::cout << "\n[4] 获取用户信息验证头像..." << std::endl;
    chat::GetUserInfoReq info_req;
    chat::GetUserInfoRsp info_rsp;
    brpc::Controller cntl4;
    
    info_req.set_request_id(generate_request_id());
    info_req.set_user_id(user_id);
    info_req.set_session_id(session_id);
    
    stub.GetUserInfo(&cntl4, &info_req, &info_rsp, nullptr);
    
    if (!info_rsp.success()) {
        std::cout << "FAIL: " << info_rsp.errmsg() << std::endl;
        return -1;
    }
    
    const auto& info = info_rsp.user_info();
    std::cout << "PASS: 获取用户信息成功" << std::endl;
    std::cout << "  user_id: " << info.user_id() << std::endl;
    std::cout << "  nickname: " << info.nickname() << std::endl;
    std::cout << "  avatar_size: " << info.avatar().size() << " bytes" << std::endl;
    
    std::cout << "\n=== 头像上传测试全部通过！===" << std::endl;
    return 0;
}
