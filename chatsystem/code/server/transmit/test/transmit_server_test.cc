// =============================================================================
// transmit_server_test.cc - 消息转发服务端到端测试客户端
// =============================================================================
// 按照测试要求，对 MsgTransmitService 的核心 RPC 接口进行端到端测试：
//
// 测试接口：
//   1. GetTransmitTarget - 获取消息转发目标
//
// 测试数据结构：
//   - 测试消息：用户ID、会话ID、消息内容
//
// 运行方式：
//   ./transmit_server_test --transmit_server_addr=127.0.0.1:10005
//
// 注意：需先启动 transmit_server，依赖 MySQL、RabbitMQ、etcd、user_server 服务
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>

#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "transmit.pb.h"
#include "log.hpp"

// ==================== gflags 命令行参数 ====================

DEFINE_string(transmit_server_addr, "127.0.0.1:10005", "Transmit server address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");

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

// ==================== 测试函数声明 ====================

bool test_get_transmit_target(transmit::MsgTransmitService_Stub& stub);
bool test_get_transmit_target_with_empty_user_id(transmit::MsgTransmitService_Stub& stub);
bool test_get_transmit_target_with_empty_session_id(transmit::MsgTransmitService_Stub& stub);
bool test_get_transmit_target_with_empty_message(transmit::MsgTransmitService_Stub& stub);

// ==================== 测试函数实现 ====================

// ---------------------------------------------------------------------------
// 测试 1：正常获取消息转发目标
// ---------------------------------------------------------------------------
bool test_get_transmit_target(transmit::MsgTransmitService_Stub& stub) {
    std::cout << "\n=== Test 1: GetTransmitTarget (Normal Case) ===" << std::endl;

    // 构造请求
    transmit::NewMessageReq req;
    transmit::GetTransmitTargetRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    // 使用数据库中已存在的用户ID（发送者）
    req.set_user_id("USER19f94f6a7d7_1890436_82f6ce167b074406_000103");
    // 使用预创建的测试会话（包含多个成员）
    req.set_chat_session_id("TEST_SESSION_001");
    
    // 设置文本消息内容
    file::MessageContent* msg_content = new file::MessageContent();
    msg_content->set_message_type(file::MessageType::STRING);
    file::StringMessageInfo* string_msg = new file::StringMessageInfo();
    string_msg->set_content("Hello, this is a test message!");
    msg_content->set_allocated_string_message(string_msg);
    req.set_allocated_message(msg_content);

    cntl.set_timeout_ms(FLAGS_timeout_ms);

    // 调用 RPC
    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);

    // 检查结果
    bool success = !cntl.Failed() && rsp.success();
    
    print_test_result("GetTransmitTarget", success, 
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());

    if (success) {
        std::cout << "  ├─ request_id: " << rsp.request_id() << std::endl;
        std::cout << "  ├─ message_id: " << rsp.message().message_id() << std::endl;
        std::cout << "  ├─ chat_session_id: " << rsp.message().chat_session_id() << std::endl;
        std::cout << "  ├─ timestamp: " << rsp.message().timestamp() << std::endl;
        std::cout << "  ├─ sender.user_id: " << rsp.message().sender().user_id() << std::endl;
        std::cout << "  ├─ message.type: " << static_cast<int>(rsp.message().message().message_type()) << std::endl;
        if (rsp.message().message().has_string_message()) {
            std::cout << "  ├─ message.string_message.content: " << rsp.message().message().string_message().content() << std::endl;
        }
        std::cout << "  └─ target_count: " << rsp.target_id_list_size() << std::endl;
        
        for (int i = 0; i < rsp.target_id_list_size(); ++i) {
            std::cout << "     └─ target_id[" << i << "]: " << rsp.target_id_list(i) << std::endl;
        }
    }

    return success;
}

// ---------------------------------------------------------------------------
// 测试 2：空用户ID参数校验
// ---------------------------------------------------------------------------
bool test_get_transmit_target_with_empty_user_id(transmit::MsgTransmitService_Stub& stub) {
    std::cout << "\n=== Test 2: GetTransmitTarget (Empty User ID) ===" << std::endl;

    // 构造请求（空用户ID）
    transmit::NewMessageReq req;
    transmit::GetTransmitTargetRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    // user_id 为空
    req.set_chat_session_id("test_session_" + random_string(8));
    
    // 设置文本消息内容
    file::MessageContent* msg_content = new file::MessageContent();
    msg_content->set_message_type(file::MessageType::STRING);
    file::StringMessageInfo* string_msg = new file::StringMessageInfo();
    string_msg->set_content("Test message without user_id");
    msg_content->set_allocated_string_message(string_msg);
    req.set_allocated_message(msg_content);

    cntl.set_timeout_ms(FLAGS_timeout_ms);

    // 调用 RPC
    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);

    // 预期：参数校验失败，返回错误
    bool success = !cntl.Failed() && !rsp.success() && rsp.errmsg() == "Missing user_id";
    
    print_test_result("GetTransmitTarget with empty user_id", success, 
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());

    return success;
}

// ---------------------------------------------------------------------------
// 测试 3：空会话ID参数校验
// ---------------------------------------------------------------------------
bool test_get_transmit_target_with_empty_session_id(transmit::MsgTransmitService_Stub& stub) {
    std::cout << "\n=== Test 3: GetTransmitTarget (Empty Session ID) ===" << std::endl;

    // 构造请求（空会话ID）
    transmit::NewMessageReq req;
    transmit::GetTransmitTargetRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_user_id("test_user_" + random_string(8));
    // chat_session_id 为空
    
    // 设置文本消息内容
    file::MessageContent* msg_content = new file::MessageContent();
    msg_content->set_message_type(file::MessageType::STRING);
    file::StringMessageInfo* string_msg = new file::StringMessageInfo();
    string_msg->set_content("Test message without session_id");
    msg_content->set_allocated_string_message(string_msg);
    req.set_allocated_message(msg_content);

    cntl.set_timeout_ms(FLAGS_timeout_ms);

    // 调用 RPC
    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);

    // 预期：参数校验失败，返回错误
    bool success = !cntl.Failed() && !rsp.success() && rsp.errmsg() == "Missing chat_session_id";
    
    print_test_result("GetTransmitTarget with empty session_id", success, 
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());

    return success;
}

// ---------------------------------------------------------------------------
// 测试 4：空消息内容参数校验
// ---------------------------------------------------------------------------
bool test_get_transmit_target_with_empty_message(transmit::MsgTransmitService_Stub& stub) {
    std::cout << "\n=== Test 4: GetTransmitTarget (Empty Message) ===" << std::endl;

    // 构造请求（空消息内容）
    transmit::NewMessageReq req;
    transmit::GetTransmitTargetRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_user_id("test_user_" + random_string(8));
    req.set_chat_session_id("test_session_" + random_string(8));
    // message 为空（不设置）

    cntl.set_timeout_ms(FLAGS_timeout_ms);

    // 调用 RPC
    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);

    // 预期：参数校验失败，返回错误
    bool success = !cntl.Failed() && !rsp.success() && rsp.errmsg() == "Missing message content";
    
    print_test_result("GetTransmitTarget with empty message", success, 
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());

    return success;
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    // 初始化 gflags
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 初始化日志（调试模式，控制台输出）
    mylog::init(true, "transmit_server_test.log", mylog::LogLevel::DEBUG);

    std::cout << "==============================================" << std::endl;
    std::cout << "Transmit Service End-to-End Test Client" << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "Server: " << FLAGS_transmit_server_addr << std::endl;
    std::cout << "Timeout: " << FLAGS_timeout_ms << "ms" << std::endl;

    // 创建 brpc channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.connection_type = brpc::CONNECTION_TYPE_SHORT;
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = 3;

    if (channel.Init(FLAGS_transmit_server_addr.c_str(), "", &options) != 0) {
        std::cerr << "[ERROR] Failed to initialize channel to " << FLAGS_transmit_server_addr << std::endl;
        return -1;
    }

    // 创建 stub
    transmit::MsgTransmitService_Stub stub(&channel);

    // 执行测试用例
    int passed = 0;
    int total = 0;

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Running test cases..." << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    // 测试 1：正常获取消息转发目标
    total++;
    if (test_get_transmit_target(stub)) passed++;

    // 测试 2：空用户ID参数校验
    total++;
    if (test_get_transmit_target_with_empty_user_id(stub)) passed++;

    // 测试 3：空会话ID参数校验
    total++;
    if (test_get_transmit_target_with_empty_session_id(stub)) passed++;

    // 测试 4：空消息内容参数校验
    total++;
    if (test_get_transmit_target_with_empty_message(stub)) passed++;

    // 输出测试统计
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total: " << total << ", Passed: " << passed << ", Failed: " << (total - passed) << std::endl;

    if (passed == total) {
        std::cout << "\n[SUCCESS] All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n[FAILURE] Some tests failed!" << std::endl;
        return -1;
    }
}
