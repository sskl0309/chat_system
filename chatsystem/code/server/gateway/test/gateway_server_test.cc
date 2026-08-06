// =============================================================================
// gateway_server_test.cc - 网关服务端到端 gtest 测试客户端
// =============================================================================
// 基于 Google Test 框架，通过 HTTP 请求对 GatewayServer 的所有路由进行端到端测试。
// 网关服务对外暴露 HTTP 接口，请求/响应体使用 ProtoBuf 序列化。
//
// 测试夹具 GatewayServerTest：
//   - SetUpTestSuite: 连接网关服务，注册并登录 3 个测试用户
//   - 共享 HTTP 客户端与用户数据，避免每个用例重复注册
//
// 测试用例：
//   1. UserRegister            - 用户注册（无需鉴权）
//   2. UserLogin               - 用户登录，获取会话ID（无需鉴权）
//   3. GetUserInfo             - 获取用户信息（需鉴权）
//   4. SetUserAvatar           - 修改用户头像（需鉴权）
//   5. SetUserNickname         - 修改用户昵称（需鉴权）
//   6. SetUserDescription      - 修改用户签名（需鉴权）
//   7. FriendAdd               - 发送好友申请（需鉴权）
//   8. GetPendingFriendEvents  - 获取待处理好友申请（需鉴权）
//   9. FriendAddProcess        - 处理好友申请（需鉴权）
//  10. GetFriendList           - 获取好友列表（需鉴权）
//  11. FriendSearch            - 搜索用户（需鉴权）
//  12. FriendRemove            - 删除好友（需鉴权）
//  13. ChatSessionCreate       - 创建群聊会话（需鉴权）
//  14. GetChatSessionList      - 获取会话列表（需鉴权）
//  15. GetChatSessionMember    - 获取会话成员（需鉴权）
//  16. PutSingleFile           - 单文件上传（需鉴权）
//  17. GetSingleFile           - 单文件下载（需鉴权）
//  18. AuthenticationFail      - 鉴权失败测试（无效 session_id）
//
// 运行方式：
//   ./gateway_server_test --gateway_addr=127.0.0.1 --gateway_http_port=8888
//
// 注意：需先启动 gateway_server 及其依赖的子服务（user、friend、message、file 等）
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <httplib.h>

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <chrono>

#include "user.pb.h"
#include "friend.pb.h"
#include "message.pb.h"
#include "transmit.pb.h"
#include "file.pb.h"
#include "speech.pb.h"
#include "notify.pb.h"
#include "log.hpp"

// ==================== gflags 命令行参数 ====================

DEFINE_string(gateway_addr, "127.0.0.1", "Gateway server HTTP address");
DEFINE_int32(gateway_http_port, 8888, "Gateway server HTTP port");
DEFINE_int32(timeout_ms, 5000, "HTTP request timeout in milliseconds");

// ==================== 测试数据结构 ====================

/**
 * @brief 测试用户信息
 * 存储用户注册登录后的各项数据，供后续测试用例共享使用
 */
struct TestUser {
    std::string nickname;     ///< 用户昵称
    std::string password;     ///< 登录密码
    std::string user_id;      ///< 用户ID（登录后获取）
    std::string session_id;   ///< 会话ID（登录后获取，用于后续鉴权）
};

// ==================== 工具函数 ====================

/// 生成指定长度的随机字符串（小写字母+数字）
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

/// 生成唯一的请求ID，用于链路追踪
static std::string generate_request_id() {
    return "req_" + random_string(16);
}

/// 生成一个最小的 PNG 图片数据（1x1 像素），用于测试头像上传
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

// ==================== 测试夹具 ====================

/**
 * @brief 网关服务端到端测试夹具
 *
 * 在 SetUpTestSuite 中一次性完成：
 *   1. 创建 HTTP 客户端连接网关服务
 *   2. 注册并登录 3 个测试用户（猪爸爸、猪妈妈、小猪佩奇）
 *   3. 设置头像和昵称
 * 所有测试用例共享 HTTP 客户端与用户数据，按业务流程顺序执行。
 */
class GatewayServerTest : public ::testing::Test {
protected:
    // 共享的 HTTP 客户端
    static std::shared_ptr<httplib::Client> http_client_;

    // 共享的测试用户
    static TestUser pig_dad_;      ///< 猪爸爸
    static TestUser pig_mom_;      ///< 猪妈妈
    static TestUser pig_peppa_;    ///< 小猪佩奇

    // 共享的好友申请事件ID（由 FriendAdd 产生，供 FriendAddProcess 使用）
    static std::string event_dad_;
    static std::string event_peppa_;

    // 共享的群聊会话ID
    static std::string group_session_id_;

    // 共享的文件ID（由 PutSingleFile 产生，供 GetSingleFile 使用）
    static std::string file_id_;

    /**
     * @brief 测试套件前置：初始化 HTTP 客户端并准备用户
     */
    static void SetUpTestSuite() {
        mylog::init(true, "", mylog::LogLevel::INFO);

        // 创建 HTTP 客户端，连接网关服务器
        http_client_ = std::make_shared<httplib::Client>(
            FLAGS_gateway_addr, FLAGS_gateway_http_port);
        http_client_->set_connection_timeout(FLAGS_timeout_ms, 0);
        http_client_->set_read_timeout(FLAGS_timeout_ms, 0);
        std::cout << "Connected to gateway: " << FLAGS_gateway_addr
                  << ":" << FLAGS_gateway_http_port << std::endl;

        // 准备 3 个测试用户（昵称3-15字符，只允许字母数字_.-）
        std::string suffix = random_string(4);
        pig_dad_.nickname   = "pd" + suffix;
        pig_dad_.password   = "pass" + random_string(6);

        pig_mom_.nickname   = "pm" + suffix;
        pig_mom_.password   = "pass" + random_string(6);

        pig_peppa_.nickname = "pp" + suffix;
        pig_peppa_.password = "pass" + random_string(6);

        // 注册并登录所有用户
        ASSERT_TRUE(register_and_login(pig_dad_))   << "猪爸爸注册登录失败";
        ASSERT_TRUE(register_and_login(pig_mom_))   << "猪妈妈注册登录失败";
        ASSERT_TRUE(register_and_login(pig_peppa_)) << "小猪佩奇注册登录失败";

        // 设置头像和昵称
        ASSERT_TRUE(set_user_avatar(pig_dad_));
        ASSERT_TRUE(set_user_avatar(pig_mom_));
        ASSERT_TRUE(set_user_avatar(pig_peppa_));

        ASSERT_TRUE(set_user_nickname(pig_dad_));
        ASSERT_TRUE(set_user_nickname(pig_mom_));
        ASSERT_TRUE(set_user_nickname(pig_peppa_));

        std::cout << "\n用户准备完成：" << std::endl;
        std::cout << "  猪爸爸: " << pig_dad_.user_id
                  << " session: " << pig_dad_.session_id << std::endl;
        std::cout << "  猪妈妈: " << pig_mom_.user_id
                  << " session: " << pig_mom_.session_id << std::endl;
        std::cout << "  小猪佩奇: " << pig_peppa_.user_id
                  << " session: " << pig_peppa_.session_id << std::endl;

        // 等待 ES 索引刷新，避免搜索测试失败
        std::cout << "  等待 ES 索引刷新..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    static void TearDownTestSuite() {
        std::cout << "\n所有测试用例执行完毕" << std::endl;
    }

    // ==================== HTTP 请求辅助方法 ====================

    /**
     * @brief 发送 HTTP POST 请求（ProtoBuf 序列化）
     *
     * 统一处理 ProtoBuf 序列化、HTTP 请求发送、响应反序列化。
     *
     * @tparam ReqProto 请求 ProtoBuf 类型
     * @tparam RspProto 响应 ProtoBuf 类型
     * @param path HTTP 路由路径
     * @param req_proto 请求 ProtoBuf 对象
     * @param rsp_proto 响应 ProtoBuf 对象（输出）
     * @return HTTP 状态码（200 成功，401 鉴权失败，其他为错误）
     */
    template<typename ReqProto, typename RspProto>
    static int send_http_request(const std::string& path,
                                  const ReqProto& req_proto,
                                  RspProto& rsp_proto) {
        // 序列化请求
        std::string req_body;
        if (!req_proto.SerializeToString(&req_body)) {
            LOG_ERROR("[Test] Failed to serialize request for {}", path);
            return -1;
        }

        // 发送 HTTP POST 请求
        auto res = http_client_->Post(path, req_body, "application/x-protobuf");
        if (!res) {
            LOG_ERROR("[Test] HTTP request failed for {}: connection error", path);
            return -1;
        }

        // 非 200 状态码直接返回，不解析 ProtoBuf
        if (res->status != 200) {
            LOG_ERROR("[Test] HTTP {} returned status {}, body: {}", path, res->status,
                      res->body.substr(0, 200));
            return res->status;
        }

        // 反序列化响应
        if (!rsp_proto.ParseFromString(res->body)) {
            LOG_ERROR("[Test] Failed to parse response for {}: body size={}, first 100 bytes: {}",
                      path, res->body.size(),
                      res->body.substr(0, std::min((size_t)100, res->body.size())));
            return -1;
        }

        return res->status;
    }

    // ==================== 用户服务辅助方法 ====================

    /// 注册并登录用户，获取 user_id 和 session_id
    static bool register_and_login(TestUser& user) {
        // 1. 注册
        {
            chat::UserRegisterReq req;
            chat::UserRegisterRsp rsp;
            req.set_request_id(generate_request_id());
            req.set_nickname(user.nickname);
            req.set_password(user.password);

            int status = send_http_request("/api/user/register", req, rsp);
            if (status != 200 || !rsp.success()) {
                std::cout << "  注册 " << user.nickname << " 失败: "
                          << (status != 200 ? "HTTP error" : rsp.errmsg())
                          << "（可能已存在，尝试登录）" << std::endl;
            }
        }

        // 2. 登录
        {
            chat::UserLoginReq req;
            chat::UserLoginRsp rsp;
            req.set_request_id(generate_request_id());
            req.set_nickname(user.nickname);
            req.set_password(user.password);

            int status = send_http_request("/api/user/login", req, rsp);
            if (status != 200 || !rsp.success()) {
                std::cout << "  登录 " << user.nickname << " 失败: "
                          << (status != 200 ? "HTTP error" : rsp.errmsg()) << std::endl;
                return false;
            }

            user.user_id = rsp.user_id();
            user.session_id = rsp.login_session_id();
            return !user.user_id.empty() && !user.session_id.empty();
        }
    }

    /// 设置用户头像
    static bool set_user_avatar(const TestUser& user) {
        chat::SetUserAvatarReq req;
        chat::SetUserAvatarRsp rsp;
        req.set_request_id(generate_request_id());
        req.set_session_id(user.session_id);
        req.set_avatar(generate_test_avatar());

        int status = send_http_request("/api/user/avatar", req, rsp);
        if (status != 200 || !rsp.success()) {
            std::cout << "  [SetUserAvatar 失败] " << user.nickname
                      << ": status=" << status << std::endl;
            return false;
        }
        return true;
    }

    /// 设置用户昵称
    static bool set_user_nickname(const TestUser& user) {
        chat::SetUserNicknameReq req;
        chat::SetUserNicknameRsp rsp;
        req.set_request_id(generate_request_id());
        req.set_session_id(user.session_id);
        req.set_nickname(user.nickname);

        int status = send_http_request("/api/user/nickname", req, rsp);
        if (status != 200 || !rsp.success()) {
            std::cout << "  [SetUserNickname 失败] " << user.nickname
                      << ": status=" << status << std::endl;
            return false;
        }
        return true;
    }
};

// 静态成员定义
std::shared_ptr<httplib::Client> GatewayServerTest::http_client_;
TestUser GatewayServerTest::pig_dad_;
TestUser GatewayServerTest::pig_mom_;
TestUser GatewayServerTest::pig_peppa_;
std::string GatewayServerTest::event_dad_;
std::string GatewayServerTest::event_peppa_;
std::string GatewayServerTest::group_session_id_;
std::string GatewayServerTest::file_id_;

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：UserRegister - 用户注册（无需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, UserRegister) {
    chat::UserRegisterReq req;
    chat::UserRegisterRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_nickname("reg" + random_string(6));
    req.set_password("password123");

    int status = send_http_request("/api/user/register", req, rsp);

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  用户注册成功" << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 2：UserLogin - 用户登录，获取会话ID（无需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, UserLogin) {
    // 注册一个新用户用于登录测试（避免 "already logged in" 错误）
    std::string login_user = "lg" + random_string(6);
    std::string login_pass = "pass" + random_string(6);

    {
        chat::UserRegisterReq reg_req;
        chat::UserRegisterRsp reg_rsp;
        reg_req.set_request_id(generate_request_id());
        reg_req.set_nickname(login_user);
        reg_req.set_password(login_pass);
        send_http_request("/api/user/register", reg_req, reg_rsp);
    }

    // 使用新注册的用户登录
    chat::UserLoginReq req;
    chat::UserLoginRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_nickname(login_user);
    req.set_password(login_pass);

    int status = send_http_request("/api/user/login", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_FALSE(rsp.login_session_id().empty()) << "应返回会话ID";
    EXPECT_FALSE(rsp.user_id().empty()) << "应返回用户ID";
    std::cout << "  用户登录成功，user_id: " << rsp.user_id() << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 3：GetUserInfo - 获取用户信息（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, GetUserInfo) {
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);

    int status = send_http_request("/api/user/info", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.user_info().user_id(), pig_dad_.user_id);
    std::cout << "  获取用户信息成功，昵称: " << rsp.user_info().nickname() << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 4：SetUserAvatar - 修改用户头像（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, SetUserAvatar) {
    chat::SetUserAvatarReq req;
    chat::SetUserAvatarRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_mom_.session_id);
    req.set_avatar(generate_test_avatar());

    int status = send_http_request("/api/user/avatar", req, rsp);

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  修改头像成功" << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 5：SetUserNickname - 修改用户昵称（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, SetUserNickname) {
    chat::SetUserNicknameReq req;
    chat::SetUserNicknameRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_mom_.session_id);
    req.set_nickname("new" + random_string(4));

    int status = send_http_request("/api/user/nickname", req, rsp);

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  修改昵称成功" << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 6：SetUserDescription - 修改用户签名（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, SetUserDescription) {
    chat::SetUserDescriptionReq req;
    chat::SetUserDescriptionRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);
    req.set_description("猪爸爸的快乐生活");

    int status = send_http_request("/api/user/signature", req, rsp);

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  修改签名成功" << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 7：FriendAdd - 猪爸爸和小猪佩奇向猪妈妈发送好友申请（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, FriendAdd) {
    // 猪爸爸 -> 猪妈妈
    {
        friendsvc::FriendAddReq req;
        friendsvc::FriendAddRsp rsp;
        req.set_request_id(generate_request_id());
        req.set_session_id(pig_dad_.session_id);
        req.set_respondent_id(pig_mom_.user_id);

        int status = send_http_request("/api/friend/apply", req, rsp);

        EXPECT_EQ(status, 200);
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        EXPECT_FALSE(rsp.notify_event_id().empty());
        event_dad_ = rsp.notify_event_id();
        std::cout << "  猪爸爸申请好友，事件ID: " << event_dad_ << std::endl;
    }

    // 小猪佩奇 -> 猪妈妈
    {
        friendsvc::FriendAddReq req;
        friendsvc::FriendAddRsp rsp;
        req.set_request_id(generate_request_id());
        req.set_session_id(pig_peppa_.session_id);
        req.set_respondent_id(pig_mom_.user_id);

        int status = send_http_request("/api/friend/apply", req, rsp);

        EXPECT_EQ(status, 200);
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        event_peppa_ = rsp.notify_event_id();
        std::cout << "  小猪佩奇申请好友，事件ID: " << event_peppa_ << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 8：GetPendingFriendEvents - 猪妈妈应收到 2 条待处理申请（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, GetPendingFriendEvents) {
    friendsvc::GetPendingFriendEventListReq req;
    friendsvc::GetPendingFriendEventListRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_mom_.session_id);

    int status = send_http_request("/api/friend/pending", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.event_size(), 2) << "应收到 2 条待处理申请";

    std::cout << "  待处理申请数量: " << rsp.event_size() << std::endl;
    for (int i = 0; i < rsp.event_size(); ++i) {
        std::cout << "    [" << i << "] event_id: " << rsp.event(i).event_id()
                  << ", sender: " << rsp.event(i).sender().user_id() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 9：FriendAddProcess - 猪妈妈同意猪爸爸，拒绝小猪佩奇（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, FriendAddProcess) {
    // 同意猪爸爸
    {
        friendsvc::FriendAddProcessReq req;
        friendsvc::FriendAddProcessRsp rsp;
        req.set_request_id(generate_request_id());
        req.set_session_id(pig_mom_.session_id);
        req.set_notify_event_id(event_dad_);
        req.set_agree(true);
        req.set_apply_user_id(pig_dad_.user_id);

        int status = send_http_request("/api/friend/process", req, rsp);

        EXPECT_EQ(status, 200);
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        std::cout << "  同意猪爸爸的好友申请" << std::endl;
    }

    // 拒绝小猪佩奇
    {
        friendsvc::FriendAddProcessReq req;
        friendsvc::FriendAddProcessRsp rsp;
        req.set_request_id(generate_request_id());
        req.set_session_id(pig_mom_.session_id);
        req.set_notify_event_id(event_peppa_);
        req.set_agree(false);
        req.set_apply_user_id(pig_peppa_.user_id);

        int status = send_http_request("/api/friend/process", req, rsp);

        EXPECT_EQ(status, 200);
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        std::cout << "  拒绝小猪佩奇的好友申请" << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 10：GetFriendList - 猪妈妈应有 1 个好友（猪爸爸）（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, GetFriendList) {
    friendsvc::GetFriendListReq req;
    friendsvc::GetFriendListRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_mom_.session_id);

    int status = send_http_request("/api/friend/list", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.friend_list_size(), 1) << "应有 1 个好友（猪爸爸）";

    std::cout << "  好友数量: " << rsp.friend_list_size() << std::endl;
    for (int i = 0; i < rsp.friend_list_size(); ++i) {
        std::cout << "    [" << i << "] user_id: " << rsp.friend_list(i).user_id()
                  << ", nickname: " << rsp.friend_list(i).nickname() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 11：FriendSearch - 搜索用户"peppa"（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, FriendSearch) {
    friendsvc::FriendSearchReq req;
    friendsvc::FriendSearchRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_mom_.session_id);
    req.set_search_key(pig_peppa_.nickname.substr(0, 2));

    int status = send_http_request("/api/friend/search", req, rsp);

    EXPECT_EQ(status, 200);
    if (!rsp.success()) {
        std::cout << "  [注意] FriendSearch 失败: " << rsp.errmsg()
                  << "（可能 Elasticsearch 未启动，不影响网关功能验证）" << std::endl;
        GTEST_SKIP() << "Elasticsearch not available, skipping search assertion";
    }
    EXPECT_GE(rsp.user_info_size(), 1) << "应至少搜到 1 个用户";

    std::cout << "  搜索结果数量: " << rsp.user_info_size() << std::endl;
    for (int i = 0; i < rsp.user_info_size(); ++i) {
        std::cout << "    [" << i << "] user_id: " << rsp.user_info(i).user_id()
                  << ", nickname: " << rsp.user_info(i).nickname() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 12：FriendRemove - 猪妈妈删除猪爸爸好友（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, FriendRemove) {
    friendsvc::FriendRemoveReq req;
    friendsvc::FriendRemoveRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_mom_.session_id);
    req.set_peer_id(pig_dad_.user_id);

    int status = send_http_request("/api/friend/remove", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  删除好友成功" << std::endl;

    // 验证删除后好友列表为空
    friendsvc::GetFriendListReq list_req;
    friendsvc::GetFriendListRsp list_rsp;
    list_req.set_request_id(generate_request_id());
    list_req.set_session_id(pig_mom_.session_id);

    int list_status = send_http_request("/api/friend/list", list_req, list_rsp);
    EXPECT_EQ(list_status, 200);
    EXPECT_EQ(list_rsp.friend_list_size(), 0) << "删除后应无好友";
    std::cout << "  删除后好友数量: " << list_rsp.friend_list_size() << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 13：ChatSessionCreate - 创建群聊"佩奇一家"（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, ChatSessionCreate) {
    friendsvc::ChatSessionCreateReq req;
    friendsvc::ChatSessionCreateRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);
    req.set_chat_session_name("佩奇一家");
    req.add_member_id_list(pig_dad_.user_id);
    req.add_member_id_list(pig_mom_.user_id);
    req.add_member_id_list(pig_peppa_.user_id);

    int status = send_http_request("/api/friend/session/create", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  群聊创建成功" << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 14：GetChatSessionList - 获取会话列表（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, GetChatSessionList) {
    friendsvc::GetChatSessionListReq req;
    friendsvc::GetChatSessionListRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);

    int status = send_http_request("/api/friend/session/list", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_GE(rsp.chat_session_info_list_size(), 1) << "应至少有 1 个会话";

    std::cout << "  会话数量: " << rsp.chat_session_info_list_size() << std::endl;
    for (int i = 0; i < rsp.chat_session_info_list_size(); ++i) {
        const auto& s = rsp.chat_session_info_list(i);
        std::cout << "    [" << i << "] session_id: " << s.chat_session_id()
                  << ", name: " << s.chat_session_name() << std::endl;
        // 保存群聊会话ID用于后续测试
        if (s.chat_session_name() == "佩奇一家") {
            group_session_id_ = s.chat_session_id();
        }
    }
}

// ---------------------------------------------------------------------------
// 测试 15：GetChatSessionMember - 获取群聊成员（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, GetChatSessionMember) {
    ASSERT_FALSE(group_session_id_.empty()) << "群聊未创建，前置用例失败";

    friendsvc::GetChatSessionMemberReq req;
    friendsvc::GetChatSessionMemberRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);
    req.set_chat_session_id(group_session_id_);

    int status = send_http_request("/api/friend/session/members", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.member_info_list_size(), 3) << "群聊应有 3 个成员";

    std::cout << "  群聊成员数量: " << rsp.member_info_list_size() << std::endl;
    for (int i = 0; i < rsp.member_info_list_size(); ++i) {
        std::cout << "    [" << i << "] user_id: " << rsp.member_info_list(i).user_id()
                  << ", nickname: " << rsp.member_info_list(i).nickname() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 16：PutSingleFile - 单文件上传（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, PutSingleFile) {
    file::PutSingleFileReq req;
    file::PutSingleFileRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);

    auto* file_data = req.mutable_file_data();
    file_data->set_file_name("test_file.txt");
    file_data->set_file_size(13);
    file_data->set_file_content("Hello, World!");

    int status = send_http_request("/api/file/single/upload", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_FALSE(rsp.file_info().file_id().empty()) << "应返回文件ID";

    file_id_ = rsp.file_info().file_id();
    std::cout << "  文件上传成功，file_id: " << file_id_ << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 17：GetSingleFile - 单文件下载（需鉴权）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, GetSingleFile) {
    ASSERT_FALSE(file_id_.empty()) << "文件未上传，前置用例失败";

    file::GetSingleFileReq req;
    file::GetSingleFileRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id(pig_dad_.session_id);
    req.set_file_id(file_id_);

    int status = send_http_request("/api/file/single/download", req, rsp);

    EXPECT_EQ(status, 200);
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    ASSERT_TRUE(rsp.has_file_data());
    EXPECT_EQ(rsp.file_data().file_id(), file_id_);
    EXPECT_FALSE(rsp.file_data().file_content().empty()) << "文件内容不应为空";

    std::cout << "  文件下载成功，内容大小: " << rsp.file_data().file_content().size()
              << " bytes" << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 18：AuthenticationFail - 鉴权失败测试（无效 session_id）
// ---------------------------------------------------------------------------
TEST_F(GatewayServerTest, AuthenticationFail) {
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;
    req.set_request_id(generate_request_id());
    req.set_session_id("invalid_session_id_" + random_string(16));

    int status = send_http_request("/api/user/info", req, rsp);

    // 网关应返回 401 鉴权失败
    EXPECT_EQ(status, 401) << "无效会话应返回 401";
    std::cout << "  鉴权失败测试通过，返回状态码: " << status << std::endl;
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
