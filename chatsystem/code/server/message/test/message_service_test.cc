// =============================================================================
// message_service_test.cc - 消息存储服务端到端测试客户端
// =============================================================================
// 按照测试要求，对 MsgStorageService 的 3 个 RPC 接口进行端到端测试：
//
// 测试接口：
//   1. GetRecentMsg    - 获取最近N条消息
//   2. GetHistoryMsg   - 获取指定时间段的历史消息
//   3. MsgSearch       - 关键字消息搜索
//
// 测试流程：
//   1. 先通过 MQ 发送测试消息（使用 transmit 服务或直接调用 MQ）
//   2. 等待消息存储服务消费完成
//   3. 调用 RPC 接口验证消息存储结果
//
// 运行方式：
//   ./message_service_test --message_server_addr=127.0.0.1:10004
//
// 注意：需先启动 message_server，依赖 MySQL、Elasticsearch、RabbitMQ 服务
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdio>

#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "message.pb.h"
#include "file.pb.h"
#include "log.hpp"

// ==================== gflags 命令行参数 ====================

DEFINE_string(message_server_addr, "127.0.0.1:10004", "Message storage server address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_string(mysql_user, "root", "MySQL user for test data insertion");
DEFINE_string(mysql_password, "123456", "MySQL password for test data insertion");
DEFINE_string(mysql_database, "chat_message", "MySQL database name");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL host");
DEFINE_string(es_host, "127.0.0.1", "Elasticsearch host");
DEFINE_int32(es_port, 9200, "Elasticsearch port");

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

static void print_test_result(const std::string& test_name, bool success, 
                              const std::string& detail = "") {
    if (success) {
        std::cout << "  [PASS] " << test_name << std::endl;
    } else {
        std::cout << "  [FAIL] " << test_name;
        if (!detail.empty()) {
            std::cout << " - " << detail;
        }
        std::cout << std::endl;
    }
}

// ==================== 集成测试辅助函数 ====================

static std::string escape_sql_string(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '\'') result += "''";
        else result += c;
    }
    return result;
}

static std::string format_time_string(int64_t ms) {
    std::time_t t = ms / 1000;
    std::tm tm_info;
    localtime_r(&t, &tm_info);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    return buf;
}

static bool insert_test_message_to_mysql(
    const std::string& msg_id,
    const std::string& session_id,
    const std::string& sender_id,
    int msg_type,
    const std::string& content,
    int64_t create_time_ms) {
    
    std::string sql = "INSERT INTO msg_record "
        "(message_id, created_time, from_user_id, to_session_id, message_type, content) "
        "VALUES ('" + escape_sql_string(msg_id) + "', '"
        + format_time_string(create_time_ms) + "', '"
        + escape_sql_string(sender_id) + "', '"
        + escape_sql_string(session_id) + "', "
        + std::to_string(msg_type) + ", '"
        + escape_sql_string(content) + "')";
    
    std::string cmd = std::string("mysql -u") + FLAGS_mysql_user + " -p" + FLAGS_mysql_password 
                    + " -h" + FLAGS_mysql_host + " " + FLAGS_mysql_database
                    + " -e \"" + sql + "\" 2>/dev/null";
    
    int ret = std::system(cmd.c_str());
    return ret == 0;
}

static bool insert_test_message_to_es(
    const std::string& msg_id,
    const std::string& session_id,
    const std::string& content) {
    
    std::string es_url = "http://" + FLAGS_es_host + ":" + std::to_string(FLAGS_es_port) 
                       + "/message/_doc/" + msg_id;
    
    std::string json_data = 
        "{"
        "\"chat_session_id\":\"" + session_id + "\","
        "\"message_id\":\"" + msg_id + "\","
        "\"content\":\"" + content + "\""
        "}";
    
    std::string cmd = "curl -s -X PUT '" + es_url + "' "
                    "-H 'Content-Type: application/json' "
                    "-d '" + json_data + "' 2>/dev/null";
    
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
    pclose(pipe);
    
    return result.find("\"result\":\"created\"") != std::string::npos 
        || result.find("\"result\":\"updated\"") != std::string::npos;
}

static void cleanup_test_data(const std::string& session_id) {
    std::string sql = "DELETE FROM msg_record WHERE to_session_id = '" + escape_sql_string(session_id) + "'";
    std::string cmd = std::string("mysql -u") + FLAGS_mysql_user + " -p" + FLAGS_mysql_password 
                    + " -h" + FLAGS_mysql_host + " " + FLAGS_mysql_database
                    + " -e \"" + sql + "\" 2>/dev/null";
    std::system(cmd.c_str());
    
    std::string es_cmd = std::string("curl -s -X POST '") + 
        "http://" + FLAGS_es_host + ":" + std::to_string(FLAGS_es_port) 
        + "/message/_delete_by_query' "
        "-H 'Content-Type: application/json' "
        "-d '{\"query\":{\"term\":{\"chat_session_id\":\"" + session_id + "\"}}}' 2>/dev/null";
    std::system(es_cmd.c_str());
}

// ==================== 测试函数实现 ====================

// ---------------------------------------------------------------------------
// 测试 1：GetRecentMsg - 获取最近N条消息（空会话ID参数校验）
// ---------------------------------------------------------------------------
bool test_get_recent_msg_empty_session(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 1: GetRecentMsg (Empty Session ID) ===" << std::endl;

    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_msg_count(10);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && !rsp.success();
    print_test_result("GetRecentMsg with empty session_id", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 2：GetRecentMsg - 无效消息数量参数校验
// ---------------------------------------------------------------------------
bool test_get_recent_msg_invalid_count(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 2: GetRecentMsg (Invalid msg_count) ===" << std::endl;

    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id("test_session_" + random_string(8));
    req.set_msg_count(0);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && !rsp.success();
    print_test_result("GetRecentMsg with invalid msg_count", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 3：GetHistoryMsg - 空会话ID参数校验
// ---------------------------------------------------------------------------
bool test_get_history_msg_empty_session(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 3: GetHistoryMsg (Empty Session ID) ===" << std::endl;

    message::GetHistoryMsgReq req;
    message::GetHistoryMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_start_time(1700000000000);
    req.set_over_time(1700000000000);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetHistoryMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && !rsp.success();
    print_test_result("GetHistoryMsg with empty session_id", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 4：GetHistoryMsg - 无效时间范围参数校验
// ---------------------------------------------------------------------------
bool test_get_history_msg_invalid_time(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 4: GetHistoryMsg (Invalid Time Range) ===" << std::endl;

    message::GetHistoryMsgReq req;
    message::GetHistoryMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id("test_session_" + random_string(8));
    req.set_start_time(0);
    req.set_over_time(0);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetHistoryMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && !rsp.success();
    print_test_result("GetHistoryMsg with invalid time range", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 5：MsgSearch - 空会话ID参数校验
// ---------------------------------------------------------------------------
bool test_msg_search_empty_session(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 5: MsgSearch (Empty Session ID) ===" << std::endl;

    message::MsgSearchReq req;
    message::MsgSearchRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_search_key("测试关键字");

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.MsgSearch(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && !rsp.success();
    print_test_result("MsgSearch with empty session_id", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 6：MsgSearch - 空搜索关键字参数校验
// ---------------------------------------------------------------------------
bool test_msg_search_empty_key(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 6: MsgSearch (Empty Search Key) ===" << std::endl;

    message::MsgSearchReq req;
    message::MsgSearchRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id("test_session_" + random_string(8));

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.MsgSearch(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && !rsp.success();
    print_test_result("MsgSearch with empty search_key", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 7：GetRecentMsg - 查询不存在会话的消息（应返回空列表）
// ---------------------------------------------------------------------------
bool test_get_recent_msg_nonexistent_session(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 7: GetRecentMsg (Non-existent Session) ===" << std::endl;

    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id("non_existent_session_" + random_string(8));
    req.set_msg_count(10);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() << std::endl;
        std::cout << "  └─ (expected empty list for non-existent session)" << std::endl;
    }

    print_test_result("GetRecentMsg for non-existent session", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 8：MsgSearch - 搜索不存在的关键字（应返回空列表）
// ---------------------------------------------------------------------------
bool test_msg_search_nonexistent_keyword(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 8: MsgSearch (Non-existent Keyword) ===" << std::endl;

    message::MsgSearchReq req;
    message::MsgSearchRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id("non_existent_session_" + random_string(8));
    req.set_search_key("不存在的关键字_xyz123");

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.MsgSearch(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() << std::endl;
        std::cout << "  └─ (expected empty list for non-existent keyword)" << std::endl;
    }

    print_test_result("MsgSearch for non-existent keyword", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 9：GetHistoryMsg - 查询指定时间范围（应返回空列表）
// ---------------------------------------------------------------------------
bool test_get_history_msg_empty_range(message::MsgStorageService_Stub& stub) {
    std::cout << "\n=== Test 9: GetHistoryMsg (Empty Time Range) ===" << std::endl;

    message::GetHistoryMsgReq req;
    message::GetHistoryMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id("non_existent_session_" + random_string(8));
    
    auto now = std::chrono::system_clock::now();
    auto future = now + std::chrono::hours(1);
    auto future_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    auto future_end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        future.time_since_epoch()).count();
    
    req.set_start_time(future_ms);
    req.set_over_time(future_end_ms);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetHistoryMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() << std::endl;
        std::cout << "  └─ (expected empty list for future time range)" << std::endl;
    }

    print_test_result("GetHistoryMsg for empty time range", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ==================== 集成测试：带数据的 RPC 接口测试 ====================

struct TestMessage {
    std::string msg_id;
    std::string content;
    int64_t create_time_ms;
};

static bool setup_test_data(std::string& session_id, std::vector<TestMessage>& messages) {
    session_id = "integration_test_" + random_string(8);
    cleanup_test_data(session_id);
    
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    messages.clear();
    for (int i = 0; i < 5; i++) {
        TestMessage msg;
        msg.msg_id = "test_msg_" + std::to_string(i) + "_" + random_string(6);
        msg.content = "Hello this is test message number " + std::to_string(i) 
                    + " with unique token_" + random_string(4);
        msg.create_time_ms = now_ms - (5 - i) * 1000;
        messages.push_back(msg);
        
        if (!insert_test_message_to_mysql(msg.msg_id, session_id,
            "user_a", 0, msg.content, msg.create_time_ms)) {
            std::cerr << "  [ERROR] Failed to insert MySQL data for " << msg.msg_id << std::endl;
            return false;
        }
        
        if (!insert_test_message_to_es(msg.msg_id, session_id, msg.content)) {
            std::cerr << "  [ERROR] Failed to insert ES data for " << msg.msg_id << std::endl;
            return false;
        }
        
        std::cout << "  [OK] Inserted message " << i << ": " << msg.msg_id 
                  << " (time=" << msg.create_time_ms << ")" << std::endl;
    }
    
    // 等待ES索引刷新，确保数据可被搜索
    std::cout << "  Waiting for ES refresh..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 主动触发ES索引刷新
    std::string refresh_cmd = "curl -s -X POST 'http://" + 
        std::string(FLAGS_es_host) + ":" + std::to_string(FLAGS_es_port) + 
        "/message/_refresh' 2>/dev/null";
    std::system(refresh_cmd.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    return true;
}

// ---------------------------------------------------------------------------
// 测试 10：GetRecentMsg - 带数据的最近消息查询
// ---------------------------------------------------------------------------
bool test_get_recent_msg_with_data(message::MsgStorageService_Stub& stub,
                                    const std::string& session_id,
                                    const std::vector<TestMessage>& messages) {
    std::cout << "\n=== Test 10: GetRecentMsg (With Data) ===" << std::endl;

    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id(session_id);
    req.set_msg_count(3);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() << " (expected 3)" << std::endl;
        success = rsp.msg_list_size() == 3;
        
        for (int i = 0; i < rsp.msg_list_size() && i < 3; i++) {
            const auto& msg = rsp.msg_list(i);
            std::cout << "  ├─ [" << i << "] message_id=" << msg.message_id() 
                      << ", content=" << (msg.message().has_string_message() ? 
                          msg.message().string_message().content().substr(0, 40) : "N/A")
                      << std::endl;
        }
    }

    print_test_result("GetRecentMsg with data", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 11：GetRecentMsg - 请求数量超过实际数据
// ---------------------------------------------------------------------------
bool test_get_recent_msg_more_than_available(message::MsgStorageService_Stub& stub,
                                              const std::string& session_id,
                                              int expected_count) {
    std::cout << "\n=== Test 11: GetRecentMsg (More Than Available) ===" << std::endl;

    message::GetRecentMsgReq req;
    message::GetRecentMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id(session_id);
    req.set_msg_count(100);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetRecentMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() 
                  << " (expected " << expected_count << " or less)" << std::endl;
        success = rsp.msg_list_size() <= expected_count && rsp.msg_list_size() > 0;
    }

    print_test_result("GetRecentMsg requesting more than available", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 12：GetHistoryMsg - 带数据的时间范围查询
// ---------------------------------------------------------------------------
bool test_get_history_msg_with_data(message::MsgStorageService_Stub& stub,
                                     const std::string& session_id,
                                     const std::vector<TestMessage>& messages) {
    std::cout << "\n=== Test 12: GetHistoryMsg (With Data) ===" << std::endl;

    message::GetHistoryMsgReq req;
    message::GetHistoryMsgRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id(session_id);
    
    int64_t min_time = messages.front().create_time_ms - 1000;
    int64_t max_time = messages.back().create_time_ms + 1000;
    req.set_start_time(min_time);
    req.set_over_time(max_time);

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.GetHistoryMsg(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() 
                  << " (expected " << messages.size() << ")" << std::endl;
        std::cout << "  ├─ time range: [" << min_time << ", " << max_time << "]" << std::endl;
        success = rsp.msg_list_size() == (int)messages.size();
    }

    print_test_result("GetHistoryMsg with data", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 13：MsgSearch - 带数据的关键字搜索
// ---------------------------------------------------------------------------
bool test_msg_search_with_data(message::MsgStorageService_Stub& stub,
                                const std::string& session_id,
                                const std::vector<TestMessage>& messages) {
    std::cout << "\n=== Test 13: MsgSearch (With Data) ===" << std::endl;

    message::MsgSearchReq req;
    message::MsgSearchRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id(session_id);
    req.set_search_key("test");

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.MsgSearch(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() 
                  << " (expected some results)" << std::endl;
        std::cout << "  ├─ search_key: 'test' in session " << session_id << std::endl;
        success = rsp.msg_list_size() > 0;
    }

    print_test_result("MsgSearch with data", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ---------------------------------------------------------------------------
// 测试 14：MsgSearch - 搜索特定消息内容
// ---------------------------------------------------------------------------
bool test_msg_search_specific_content(message::MsgStorageService_Stub& stub,
                                       const std::string& session_id,
                                       const std::vector<TestMessage>& messages) {
    std::cout << "\n=== Test 14: MsgSearch (Specific Content) ===" << std::endl;

    message::MsgSearchReq req;
    message::MsgSearchRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(generate_request_id());
    req.set_chat_session_id(session_id);
    req.set_search_key("token");

    cntl.set_timeout_ms(FLAGS_timeout_ms);
    stub.MsgSearch(&cntl, &req, &rsp, nullptr);

    bool success = !cntl.Failed() && rsp.success();
    
    if (success) {
        std::cout << "  ├─ msg_count: " << rsp.msg_list_size() << std::endl;
        std::cout << "  ├─ search_key: 'token' (should match all messages)" << std::endl;
        success = rsp.msg_list_size() > 0;
    }

    print_test_result("MsgSearch for specific content", success,
                      cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
    return success;
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    mylog::init(true, "message_service_test.log", mylog::LogLevel::INFO);

    std::cout << "==============================================" << std::endl;
    std::cout << "Message Storage Service End-to-End Test" << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "Server: " << FLAGS_message_server_addr << std::endl;
    std::cout << "Timeout: " << FLAGS_timeout_ms << "ms" << std::endl;

    // 创建 brpc channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.connection_type = brpc::CONNECTION_TYPE_SHORT;
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = 3;

    if (channel.Init(FLAGS_message_server_addr.c_str(), "", &options) != 0) {
        std::cerr << "[ERROR] Failed to initialize channel to " 
                  << FLAGS_message_server_addr << std::endl;
        return -1;
    }

    // 创建 stub
    message::MsgStorageService_Stub stub(&channel);

    // 执行测试用例
    int passed = 0;
    int total = 0;

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Running validation tests..." << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    // 参数校验测试（6个）
    total++;
    if (test_get_recent_msg_empty_session(stub)) passed++;

    total++;
    if (test_get_recent_msg_invalid_count(stub)) passed++;

    total++;
    if (test_get_history_msg_empty_session(stub)) passed++;

    total++;
    if (test_get_history_msg_invalid_time(stub)) passed++;

    total++;
    if (test_msg_search_empty_session(stub)) passed++;

    total++;
    if (test_msg_search_empty_key(stub)) passed++;

    // 空数据返回测试（3个）
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Running empty data tests..." << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    total++;
    if (test_get_recent_msg_nonexistent_session(stub)) passed++;

    total++;
    if (test_msg_search_nonexistent_keyword(stub)) passed++;

    total++;
    if (test_get_history_msg_empty_range(stub)) passed++;

    // 集成测试：带数据的 RPC 接口测试
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Running integration tests (with data)..." << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    std::string session_id;
    std::vector<TestMessage> test_messages;
    
    std::cout << "\n--- Setting up test data ---" << std::endl;
    if (!setup_test_data(session_id, test_messages)) {
        std::cerr << "[ERROR] Failed to set up test data, skipping integration tests" << std::endl;
    } else {
        std::cout << "  Session ID: " << session_id << std::endl;
        std::cout << "  Message count: " << test_messages.size() << std::endl;
        
        total++;
        if (test_get_recent_msg_with_data(stub, session_id, test_messages)) passed++;
        
        total++;
        if (test_get_recent_msg_more_than_available(stub, session_id, test_messages.size())) passed++;
        
        total++;
        if (test_get_history_msg_with_data(stub, session_id, test_messages)) passed++;
        
        total++;
        if (test_msg_search_with_data(stub, session_id, test_messages)) passed++;
        
        total++;
        if (test_msg_search_specific_content(stub, session_id, test_messages)) passed++;
        
        std::cout << "\n--- Cleanup test data ---" << std::endl;
        cleanup_test_data(session_id);
        std::cout << "  Test data cleaned up" << std::endl;
    }

    // 输出测试统计
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total: " << total << ", Passed: " << passed 
              << ", Failed: " << (total - passed) << std::endl;

    if (passed == total) {
        std::cout << "\n[SUCCESS] All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n[FAILURE] Some tests failed!" << std::endl;
        return -1;
    }
}
