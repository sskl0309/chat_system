// =============================================================================
// friend_server_test.cc - 好友服务端到端 gtest 测试客户端
// =============================================================================
// 基于 Google Test 框架对 FriendService 的全部 9 个 RPC 接口进行端到端集成测试。
//
// 测试夹具 FriendServerTest：
//   - SetUpTestSuite: 连接 friend_server 和 user_server，注册并登录 4 个测试用户
//   - 共享 stub 与用户数据，避免每个用例重复注册
//
// 测试用例：
//   1. FriendAdd              - 发送好友申请（3 人申请猪妈妈）
//   2. GetPendingFriendEvents - 获取待处理申请列表（应 3 条）
//   3. FriendAddProcess       - 处理申请（同意/拒绝）
//   4. GetFriendList          - 获取好友列表（同意后应有 2 人）
//   5. FriendSearch           - 搜索用户（应能搜到非好友）
//   6. FriendRemove           - 删除好友（删除后列表减 1）
//   7. ChatSessionCreate      - 创建群聊会话
//   8. GetChatSessionList     - 获取会话列表
//   9. GetChatSessionMember   - 获取群聊成员
//
// 运行方式：
//   ./friend_server_test --friend_server_addr=127.0.0.1:10005
//                        --user_server_addr=127.0.0.1:10001
//
// 注意：需先启动 friend_server 和 user_server，依赖 MySQL、ES、etcd 服务
// =============================================================================

#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <random>
#include <chrono>
#include <thread>

#include "friend.pb.h"
#include "user.pb.h"
#include "log.hpp"

// friend 是 C++ 关键字，使用命名空间别名引用 protobuf 生成的命名空间
namespace friendsvc_ns = friendsvc;

// ==================== gflags 命令行参数 ====================

DEFINE_string(friend_server_addr, "127.0.0.1:10005", "Friend server address");
DEFINE_string(user_server_addr, "127.0.0.1:10001", "User server address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");

// ==================== 测试数据结构 ====================

struct TestUser {
    std::string nickname;
    std::string password;
    std::string user_id;
    std::string email;
    std::string description;
    std::string session_id;
};

// ==================== 工具函数 ====================

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

// ==================== 测试夹具 ====================

/**
 * @brief 好友服务端到端测试夹具
 *
 * 在 SetUpTestSuite 中一次性完成：
 *   1. 连接 friend_server 和 user_server
 *   2. 注册并登录 4 个测试用户（猪爸爸、猪妈妈、小猪佩奇、小猪乔治）
 *   3. 设置头像/签名/昵称
 * 所有测试用例共享 stub 与用户数据，按业务流程顺序执行。
 *
 * 注意：gtest 默认按用例声明顺序执行，本测试依赖该顺序。
 */
class FriendServerTest : public ::testing::Test {
protected:
    // 共享的 RPC channel 与 stub
    // 注意：channel 必须作为静态成员持有，不能是 SetUpTestSuite 的局部变量，
    // 否则函数返回后 channel 被销毁，stub 持有悬空指针会导致
    // "pure virtual method called" 崩溃。
    static std::shared_ptr<brpc::Channel> friend_channel_;
    static std::shared_ptr<brpc::Channel> user_channel_;
    static std::shared_ptr<friendsvc_ns::FriendService_Stub> friend_stub_;
    static std::shared_ptr<chat::UserService_Stub> user_stub_;

    // 共享的测试用户
    static TestUser pig_dad_;
    static TestUser pig_mom_;
    static TestUser pig_peppa_;
    static TestUser pig_george_;

    // 共享的事件 ID（由 FriendAdd 产生，供 FriendAddProcess 使用）
    static std::string event_dad_;
    static std::string event_peppa_;
    static std::string event_george_;

    // 共享的会话 ID
    static std::string group_session_id_;

    /**
     * @brief 测试套件前置：初始化连接并准备用户
     */
    static void SetUpTestSuite() {
        mylog::init(true, "", mylog::LogLevel::INFO);

        // 连接好友服务（channel 作为静态成员长期持有）
        friend_channel_ = std::make_shared<brpc::Channel>();
        brpc::ChannelOptions friend_options;
        friend_options.protocol = brpc::PROTOCOL_BAIDU_STD;
        friend_options.timeout_ms = FLAGS_timeout_ms;
        ASSERT_EQ(friend_channel_->Init(FLAGS_friend_server_addr.c_str(), &friend_options), 0)
            << "Failed to connect friend_server: " << FLAGS_friend_server_addr;
        friend_stub_ = std::make_shared<friendsvc_ns::FriendService_Stub>(friend_channel_.get());
        std::cout << "Connected to friend server: " << FLAGS_friend_server_addr << std::endl;

        // 连接用户服务
        user_channel_ = std::make_shared<brpc::Channel>();
        brpc::ChannelOptions user_options;
        user_options.protocol = brpc::PROTOCOL_BAIDU_STD;
        user_options.timeout_ms = FLAGS_timeout_ms;
        ASSERT_EQ(user_channel_->Init(FLAGS_user_server_addr.c_str(), &user_options), 0)
            << "Failed to connect user_server: " << FLAGS_user_server_addr;
        user_stub_ = std::make_shared<chat::UserService_Stub>(user_channel_.get());
        std::cout << "Connected to user server: " << FLAGS_user_server_addr << std::endl;

        // 准备 4 个测试用户
        std::string suffix = random_string(4);
        pig_dad_.nickname    = "pig_dad_" + suffix;
        pig_dad_.password    = "pass" + random_string(6);
        pig_dad_.description = "猪爸爸的签名";

        pig_mom_.nickname    = "pig_mom_" + suffix;
        pig_mom_.password    = "pass" + random_string(6);
        pig_mom_.description = "猪妈妈的签名";

        pig_peppa_.nickname  = "pig_peppa_" + suffix;
        pig_peppa_.password  = "pass" + random_string(6);
        pig_peppa_.description = "小猪佩奇的签名";

        pig_george_.nickname = "pig_george_" + suffix;
        pig_george_.password = "pass" + random_string(6);
        pig_george_.description = "小猪乔治的签名";

        ASSERT_TRUE(register_and_login(pig_dad_))    << "猪爸爸注册登录失败";
        ASSERT_TRUE(register_and_login(pig_mom_))    << "猪妈妈注册登录失败";
        ASSERT_TRUE(register_and_login(pig_peppa_))  << "小猪佩奇注册登录失败";
        ASSERT_TRUE(register_and_login(pig_george_)) << "小猪乔治注册登录失败";

        ASSERT_TRUE(set_user_avatar(pig_dad_));
        ASSERT_TRUE(set_user_avatar(pig_mom_));
        ASSERT_TRUE(set_user_avatar(pig_peppa_));
        ASSERT_TRUE(set_user_avatar(pig_george_));

        ASSERT_TRUE(set_user_info(pig_dad_));
        ASSERT_TRUE(set_user_info(pig_mom_));
        ASSERT_TRUE(set_user_info(pig_peppa_));
        ASSERT_TRUE(set_user_info(pig_george_));

        std::cout << "\n用户准备完成：" << std::endl;
        std::cout << "  猪爸爸: " << pig_dad_.user_id << std::endl;
        std::cout << "  猪妈妈: " << pig_mom_.user_id << std::endl;
        std::cout << "  小猪佩奇: " << pig_peppa_.user_id << std::endl;
        std::cout << "  小猪乔治: " << pig_george_.user_id << std::endl;

        // 等待 ES 刷新索引（默认 1 秒 refresh interval），
        // 否则 FriendSearch 用例会因刚写入的数据尚未可搜索而失败
        std::cout << "  等待 ES 索引刷新..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    static void TearDownTestSuite() {
        std::cout << "\n所有测试用例执行完毕" << std::endl;
    }

    // ==================== 用户服务辅助方法 ====================

    static bool register_and_login(TestUser& user) {
        chat::UserRegisterReq reg_req;
        chat::UserRegisterRsp reg_rsp;
        brpc::Controller cntl1;
        reg_req.set_request_id(generate_request_id());
        reg_req.set_nickname(user.nickname);
        reg_req.set_password(user.password);
        user_stub_->UserRegister(&cntl1, &reg_req, &reg_rsp, nullptr);
        if (cntl1.Failed() || !reg_rsp.success()) {
            std::cout << "  注册 " << user.nickname << " 返回: "
                      << (cntl1.Failed() ? cntl1.ErrorText() : reg_rsp.errmsg())
                      << "（可能已存在，尝试登录）" << std::endl;
        }

        chat::UserLoginReq login_req;
        chat::UserLoginRsp login_rsp;
        brpc::Controller cntl2;
        login_req.set_request_id(generate_request_id());
        login_req.set_nickname(user.nickname);
        login_req.set_password(user.password);
        user_stub_->UserLogin(&cntl2, &login_req, &login_rsp, nullptr);
        if (cntl2.Failed() || !login_rsp.success()) return false;

        user.user_id = login_rsp.user_id();
        user.session_id = login_rsp.login_session_id();
        return !user.user_id.empty();
    }

    static bool set_user_avatar(TestUser& user) {
        chat::SetUserAvatarReq req;
        chat::SetUserAvatarRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_user_id(user.user_id);
        req.set_session_id(user.session_id);
        req.set_avatar(generate_test_avatar());
        user_stub_->SetUserAvatar(&cntl, &req, &rsp, nullptr);
        if (cntl.Failed()) {
            std::cout << "  [SetUserAvatar RPC 失败] " << user.nickname
                      << ": " << cntl.ErrorText() << std::endl;
            return false;
        }
        if (!rsp.success()) {
            std::cout << "  [SetUserAvatar 业务失败] " << user.nickname
                      << ": " << rsp.errmsg() << std::endl;
            return false;
        }
        return true;
    }

    static bool set_user_info(TestUser& user) {
        chat::SetUserDescriptionReq desc_req;
        chat::SetUserDescriptionRsp desc_rsp;
        brpc::Controller cntl1;
        desc_req.set_request_id(generate_request_id());
        desc_req.set_user_id(user.user_id);
        desc_req.set_session_id(user.session_id);
        desc_req.set_description(user.description);
        user_stub_->SetUserDescription(&cntl1, &desc_req, &desc_rsp, nullptr);

        chat::SetUserNicknameReq nick_req;
        chat::SetUserNicknameRsp nick_rsp;
        brpc::Controller cntl2;
        nick_req.set_request_id(generate_request_id());
        nick_req.set_user_id(user.user_id);
        nick_req.set_session_id(user.session_id);
        nick_req.set_nickname(user.nickname);
        user_stub_->SetUserNickname(&cntl2, &nick_req, &nick_rsp, nullptr);

        return !cntl1.Failed() && desc_rsp.success() && !cntl2.Failed() && nick_rsp.success();
    }
};

// 静态成员定义
std::shared_ptr<brpc::Channel> FriendServerTest::friend_channel_;
std::shared_ptr<brpc::Channel> FriendServerTest::user_channel_;
std::shared_ptr<friendsvc_ns::FriendService_Stub> FriendServerTest::friend_stub_;
std::shared_ptr<chat::UserService_Stub> FriendServerTest::user_stub_;
TestUser FriendServerTest::pig_dad_;
TestUser FriendServerTest::pig_mom_;
TestUser FriendServerTest::pig_peppa_;
TestUser FriendServerTest::pig_george_;
std::string FriendServerTest::event_dad_;
std::string FriendServerTest::event_peppa_;
std::string FriendServerTest::event_george_;
std::string FriendServerTest::group_session_id_;

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：FriendAdd - 3 人向猪妈妈申请好友
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, FriendAdd) {
    // 猪爸爸 -> 猪妈妈
    {
        friendsvc_ns::FriendAddReq req;
        friendsvc_ns::FriendAddRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_user_id(pig_dad_.user_id);
        req.set_respondent_id(pig_mom_.user_id);
        friend_stub_->FriendAdd(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        EXPECT_FALSE(rsp.notify_event_id().empty());
        event_dad_ = rsp.notify_event_id();
        std::cout << "  猪爸爸申请事件: " << event_dad_ << std::endl;
    }

    // 小猪佩奇 -> 猪妈妈
    {
        friendsvc_ns::FriendAddReq req;
        friendsvc_ns::FriendAddRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_user_id(pig_peppa_.user_id);
        req.set_respondent_id(pig_mom_.user_id);
        friend_stub_->FriendAdd(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        event_peppa_ = rsp.notify_event_id();
        std::cout << "  小猪佩奇申请事件: " << event_peppa_ << std::endl;
    }

    // 小猪乔治 -> 猪妈妈
    {
        friendsvc_ns::FriendAddReq req;
        friendsvc_ns::FriendAddRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_user_id(pig_george_.user_id);
        req.set_respondent_id(pig_mom_.user_id);
        friend_stub_->FriendAdd(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        event_george_ = rsp.notify_event_id();
        std::cout << "  小猪乔治申请事件: " << event_george_ << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 2：GetPendingFriendEventList - 猪妈妈应收到 3 条待处理申请
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, GetPendingFriendEvents) {
    friendsvc_ns::GetPendingFriendEventListReq req;
    friendsvc_ns::GetPendingFriendEventListRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_user_id(pig_mom_.user_id);
    friend_stub_->GetPendingFriendEventList(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.event_size(), 3) << "应收到 3 条待处理申请";

    std::cout << "  待处理申请数量: " << rsp.event_size() << std::endl;
    for (int i = 0; i < rsp.event_size(); ++i) {
        const auto& ev = rsp.event(i);
        std::cout << "    [" << i << "] event_id: " << ev.event_id()
                  << ", sender: " << ev.sender().user_id() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 3：FriendAddProcess - 同意猪爸爸、拒绝小猪佩奇、同意小猪乔治
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, FriendAddProcess) {
    // 同意猪爸爸
    {
        friendsvc_ns::FriendAddProcessReq req;
        friendsvc_ns::FriendAddProcessRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_notify_event_id(event_dad_);
        req.set_agree(true);
        friend_stub_->FriendAddProcess(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        EXPECT_TRUE(rsp.has_new_session_id()) << "同意后应返回新会话ID";
        std::cout << "  同意猪爸爸，新会话: " << rsp.new_session_id() << std::endl;
    }

    // 拒绝小猪佩奇
    {
        friendsvc_ns::FriendAddProcessReq req;
        friendsvc_ns::FriendAddProcessRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_notify_event_id(event_peppa_);
        req.set_agree(false);
        friend_stub_->FriendAddProcess(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        std::cout << "  拒绝小猪佩奇" << std::endl;
    }

    // 同意小猪乔治
    {
        friendsvc_ns::FriendAddProcessReq req;
        friendsvc_ns::FriendAddProcessRsp rsp;
        brpc::Controller cntl;
        req.set_request_id(generate_request_id());
        req.set_notify_event_id(event_george_);
        req.set_agree(true);
        friend_stub_->FriendAddProcess(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << rsp.errmsg();
        EXPECT_TRUE(rsp.has_new_session_id());
        std::cout << "  同意小猪乔治，新会话: " << rsp.new_session_id() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 4：GetFriendList - 猪妈妈应有 2 个好友（猪爸爸、小猪乔治）
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, GetFriendList) {
    friendsvc_ns::GetFriendListReq req;
    friendsvc_ns::GetFriendListRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_user_id(pig_mom_.user_id);
    friend_stub_->GetFriendList(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.friend_list_size(), 2) << "应有 2 个好友";

    std::cout << "  好友数量: " << rsp.friend_list_size() << std::endl;
    for (int i = 0; i < rsp.friend_list_size(); ++i) {
        std::cout << "    [" << i << "] user_id: " << rsp.friend_list(i).user_id()
                  << ", nickname: " << rsp.friend_list(i).nickname() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 5：FriendSearch - 搜索"佩奇"，应能搜到小猪佩奇（非好友）
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, FriendSearch) {
    friendsvc_ns::FriendSearchReq req;
    friendsvc_ns::FriendSearchRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_user_id(pig_mom_.user_id);
    req.set_search_key("peppa");
    friend_stub_->FriendSearch(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_GE(rsp.user_info_size(), 1) << "应至少搜到 1 个用户";

    std::cout << "  搜索结果数量: " << rsp.user_info_size() << std::endl;
    for (int i = 0; i < rsp.user_info_size(); ++i) {
        std::cout << "    [" << i << "] user_id: " << rsp.user_info(i).user_id()
                  << ", nickname: " << rsp.user_info(i).nickname() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 6：FriendRemove - 猪妈妈删除小猪乔治
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, FriendRemove) {
    friendsvc_ns::FriendRemoveReq req;
    friendsvc_ns::FriendRemoveRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_user_id(pig_mom_.user_id);
    req.set_peer_id(pig_george_.user_id);
    friend_stub_->FriendRemove(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    std::cout << "  删除小猪乔治成功" << std::endl;

    // 验证删除后好友列表应只剩 1 人
    friendsvc_ns::GetFriendListReq list_req;
    friendsvc_ns::GetFriendListRsp list_rsp;
    brpc::Controller list_cntl;
    list_req.set_request_id(generate_request_id());
    list_req.set_user_id(pig_mom_.user_id);
    friend_stub_->GetFriendList(&list_cntl, &list_req, &list_rsp, nullptr);

    ASSERT_FALSE(list_cntl.Failed()) << list_cntl.ErrorText();
    ASSERT_TRUE(list_rsp.success()) << list_rsp.errmsg();
    EXPECT_EQ(list_rsp.friend_list_size(), 1) << "删除后应剩 1 个好友";
    std::cout << "  删除后好友数量: " << list_rsp.friend_list_size() << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 7：ChatSessionCreate - 猪爸爸创建群聊"佩奇一家"
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, ChatSessionCreate) {
    friendsvc_ns::ChatSessionCreateReq req;
    friendsvc_ns::ChatSessionCreateRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_user_id(pig_dad_.user_id);
    req.set_chat_session_name("佩奇一家");
    req.add_member_id_list(pig_dad_.user_id);
    req.add_member_id_list(pig_mom_.user_id);
    req.add_member_id_list(pig_peppa_.user_id);
    req.add_member_id_list(pig_george_.user_id);

    friend_stub_->ChatSessionCreate(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    ASSERT_TRUE(rsp.has_chat_session_info());
    EXPECT_FALSE(rsp.chat_session_info().chat_session_id().empty());

    group_session_id_ = rsp.chat_session_info().chat_session_id();
    std::cout << "  群聊创建成功，session_id: " << group_session_id_ << std::endl;
}

// ---------------------------------------------------------------------------
// 测试 8：GetChatSessionList - 猪妈妈应至少有 2 个会话
//   （与猪爸爸的单聊 + "佩奇一家"群聊）
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, GetChatSessionList) {
    friendsvc_ns::GetChatSessionListReq req;
    friendsvc_ns::GetChatSessionListRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_user_id(pig_mom_.user_id);
    friend_stub_->GetChatSessionList(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_GE(rsp.chat_session_info_list_size(), 2) << "应至少有 2 个会话";

    std::cout << "  会话数量: " << rsp.chat_session_info_list_size() << std::endl;
    for (int i = 0; i < rsp.chat_session_info_list_size(); ++i) {
        const auto& s = rsp.chat_session_info_list(i);
        std::cout << "    [" << i << "] session_id: " << s.chat_session_id()
                  << ", name: " << s.chat_session_name() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 测试 9：GetChatSessionMember - 群聊应有 4 个成员
// ---------------------------------------------------------------------------
TEST_F(FriendServerTest, GetChatSessionMember) {
    ASSERT_FALSE(group_session_id_.empty()) << "群聊未创建，前置用例失败";

    friendsvc_ns::GetChatSessionMemberReq req;
    friendsvc_ns::GetChatSessionMemberRsp rsp;
    brpc::Controller cntl;
    req.set_request_id(generate_request_id());
    req.set_chat_session_id(group_session_id_);
    friend_stub_->GetChatSessionMember(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.errmsg();
    EXPECT_EQ(rsp.member_info_list_size(), 4) << "群聊应有 4 个成员";

    std::cout << "  群聊成员数量: " << rsp.member_info_list_size() << std::endl;
    for (int i = 0; i < rsp.member_info_list_size(); ++i) {
        std::cout << "    [" << i << "] user_id: " << rsp.member_info_list(i).user_id()
                  << ", nickname: " << rsp.member_info_list(i).nickname() << std::endl;
    }
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
