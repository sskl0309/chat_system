/**
 * @file speech_client_test.cc
 * @brief 语音识别服务测试客户端（带 etcd 服务发现）
 * 
 * 本程序用于测试语音识别子服务的功能，通过 brpc 协议连接到语音识别服务端，
 * 发送 PCM 格式的语音数据，接收并打印识别结果。
 * 
 * 新增 etcd 服务发现功能：
 * 1. 通过 etcd 查询已注册的服务列表
 * 2. 监听服务上下线事件，实时更新可用服务列表
 * 3. 设置服务上线回调和下线回调，及时感知服务变化
 * 
 * 测试流程：
 * 1. 从命令行参数读取 etcd 地址和音频文件路径
 * 2. 初始化 etcd 服务发现客户端，设置上下线回调
 * 3. 启动服务监控，发现当前已注册的语音识别服务
 * 4. 选择一个可用服务，初始化 brpc 客户端通道
 * 5. 读取音频文件的二进制数据
 * 6. 构造识别请求并发送
 * 7. 解析响应并输出识别结果
 * 
 * 编译方式：通过 CMakeLists.txt 中的 speech_client_test 目标编译
 * 运行方式：./speech_client_test --etcd_addr=127.0.0.1:2379 --audio_file=../test/16k.pcm
 */

// 标准库头文件
#include <iostream>     // 输入输出流
#include <fstream>      // 文件操作
#include <string>       // 字符串处理
#include <memory>       // 智能指针
#include <random>       // 随机数生成
#include <thread>       // 线程操作
#include <mutex>        // 互斥锁
#include <vector>       // 向量容器
#include <atomic>       // 原子操作

// brpc 框架头文件
#include <brpc/channel.h>       // brpc 客户端通道
#include <brpc/controller.h>    // brpc 控制器，用于控制 RPC 调用
#include <gflags/gflags.h>      // 命令行参数解析

// etcd 服务发现客户端
#include "../common/etcd_client.hpp"

// 语音识别服务的 protobuf 定义
#include "../build/speech.pb.h"

// ==================== gflags 命令行参数定义 ====================

/**
 * @brief etcd 服务器地址
 * 
 * 格式：IP地址:端口号
 * 默认值：127.0.0.1:2379
 */
DEFINE_string(etcd_addr, "127.0.0.1:2379", "Etcd server address");

/**
 * @brief 测试音频文件路径
 * 
 * 支持 PCM、WAV 等格式，文件内容为二进制语音数据
 * 默认值：./16k.pcm
 */
DEFINE_string(audio_file, "./16k.pcm", "Path to audio file");

/**
 * @brief RPC 调用超时时间
 * 
 * 单位：毫秒（ms）
 * 默认值：10000（10秒）
 */
DEFINE_int32(timeout_ms, 10000, "RPC timeout in milliseconds");

/**
 * @brief 目标服务名称
 * 
 * 在 etcd 中注册的服务名称，用于服务发现
 * 默认值：speech_service
 */
DEFINE_string(service_name, "speech_service", "Target service name for discovery");

// ==================== 全局变量 ====================

std::mutex g_services_mutex;                 ///< 服务列表互斥锁，保护并发访问
std::vector<std::string> g_available_services;  ///< 当前可用的服务地址列表
std::atomic<bool> g_service_found{false};    ///< 是否已发现可用服务

// ==================== 辅助函数 ====================

/**
 * @brief 读取二进制文件内容
 * 
 * 以二进制方式读取指定文件的全部内容到字符串中，
 * 适合读取语音数据等二进制文件。
 * 
 * @param file_path 文件路径
 * @return std::string 文件的二进制内容，读取失败时返回空字符串
 */
std::string read_binary_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return "";
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string buffer(size, '\0');
    if (!file.read(buffer.data(), size)) {
        std::cerr << "Failed to read file: " << file_path << std::endl;
        return "";
    }
    
    std::cout << "Read " << size << " bytes from file" << std::endl;
    return buffer;
}

/**
 * @brief 生成随机请求ID
 * 
 * 生成一个16位的十六进制随机字符串作为请求唯一标识，
 * 用于追踪和区分不同的识别请求。
 * 
 * @return std::string 16位十六进制随机字符串
 */
std::string generate_request_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::string request_id;
    const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        request_id += hex_chars[dis(gen)];
    }
    return request_id;
}

/**
 * @brief 服务上线回调函数
 * 
 * 当有新服务注册到 etcd 时触发此回调，
 * 将新服务地址添加到可用服务列表中。
 * 
 * @param service_name 服务名称
 * @param host_address 服务主机地址（格式：IP:Port）
 */
void on_service_online(const std::string& service_name, const std::string& host_address) {
    std::lock_guard<std::mutex> lock(g_services_mutex);
    
    // 检查服务名称是否匹配目标服务
    if (service_name != FLAGS_service_name) {
        return;
    }
    
    // 检查是否已存在该服务
    auto it = std::find(g_available_services.begin(), g_available_services.end(), host_address);
    if (it == g_available_services.end()) {
        g_available_services.push_back(host_address);
        g_service_found = true;
        std::cout << "\n[Service Discovery] Service ONLINE: " << service_name << " -> " << host_address << std::endl;
        std::cout << "[Service Discovery] Available services count: " << g_available_services.size() << std::endl;
    }
}

/**
 * @brief 服务下线回调函数
 * 
 * 当服务从 etcd 删除时触发此回调，
 * 从可用服务列表中移除该服务地址。
 * 
 * @param service_name 服务名称
 * @param host_address 服务主机地址（格式：IP:Port）
 */
void on_service_offline(const std::string& service_name, const std::string& host_address) {
    std::lock_guard<std::mutex> lock(g_services_mutex);
    
    // 检查服务名称是否匹配目标服务
    if (service_name != FLAGS_service_name) {
        return;
    }
    
    // 从列表中移除服务
    auto it = std::remove(g_available_services.begin(), g_available_services.end(), host_address);
    if (it != g_available_services.end()) {
        g_available_services.erase(it, g_available_services.end());
        std::cout << "\n[Service Discovery] Service OFFLINE: " << service_name << " -> " << host_address << std::endl;
        std::cout << "[Service Discovery] Available services count: " << g_available_services.size() << std::endl;
    }
    
    // 更新服务发现状态
    g_service_found = !g_available_services.empty();
}

/**
 * @brief 获取一个可用的服务地址
 * 
 * 从可用服务列表中随机选择一个服务地址，
 * 用于客户端连接。
 * 
 * @return std::string 服务地址，如果列表为空则返回空字符串
 */
std::string get_available_service() {
    std::lock_guard<std::mutex> lock(g_services_mutex);
    
    if (g_available_services.empty()) {
        return "";
    }
    
    // 随机选择一个服务（简单负载均衡）
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, g_available_services.size() - 1);
    return g_available_services[dis(gen)];
}

// ==================== 主函数 ====================

/**
 * @brief 语音识别测试客户端主函数（带 etcd 服务发现）
 * 
 * 程序入口，负责：
 * 1. 解析命令行参数
 * 2. 初始化 etcd 服务发现客户端，设置上下线回调
 * 3. 启动服务监控，发现已注册的服务
 * 4. 读取测试音频文件
 * 5. 通过服务发现获取可用服务地址，初始化 brpc 客户端通道
 * 6. 构造并发送识别请求
 * 7. 解析并输出响应结果
 * 8. 持续监控服务变化（后台运行）
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 程序退出码，0表示成功，-1表示失败
 */
int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    // 打印配置信息
    std::cout << "==== Configuration ====" << std::endl;
    std::cout << "etcd_addr: " << FLAGS_etcd_addr << std::endl;
    std::cout << "service_name: " << FLAGS_service_name << std::endl;
    std::cout << "audio_file: " << FLAGS_audio_file << std::endl;
    std::cout << "timeout_ms: " << FLAGS_timeout_ms << std::endl;
    std::cout << "=======================" << std::endl;
    
    // ==================== 初始化 etcd 服务发现客户端 ====================
    std::string etcd_url = "http://" + FLAGS_etcd_addr;
    etcd::ServiceDiscoverClient discover_client(etcd_url);
    
    // 设置服务根目录
    discover_client.set_root_directory("/services");
    
    // 设置服务上线回调
    discover_client.set_online_callback(on_service_online);
    
    // 设置服务下线回调
    discover_client.set_offline_callback(on_service_offline);
    
    // 启动服务监控
    std::cout << "\n[Service Discovery] Starting watch on etcd..." << std::endl;
    discover_client.start_watch();
    
    // 等待一段时间让 Watch 初始化完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // ==================== 发现已注册的服务 ====================
    std::cout << "[Service Discovery] Discovering existing services..." << std::endl;
    auto services = discover_client.discover_services();
    
    if (services.empty()) {
        std::cout << "[Service Discovery] No services found, waiting for service registration..." << std::endl;
        // 等待服务注册（最多等待10秒）
        int wait_count = 0;
        while (!g_service_found && wait_count < 10) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            wait_count++;
            std::cout << "[Service Discovery] Waiting... (" << wait_count << "/10)" << std::endl;
        }
        
        if (!g_service_found) {
            std::cerr << "[Service Discovery] Timeout: No service found after 10 seconds" << std::endl;
            discover_client.stop();
            return -1;
        }
    } else {
        // 将发现的服务添加到可用列表
        {
            std::lock_guard<std::mutex> lock(g_services_mutex);
            for (const auto& svc : services) {
                if (svc.first == FLAGS_service_name) {
                    g_available_services.push_back(svc.second);
                    std::cout << "[Service Discovery] Found service: " << svc.first << " -> " << svc.second << std::endl;
                }
            }
            g_service_found = !g_available_services.empty();
        }
    }
    
    // 读取音频文件内容
    std::string audio_data = read_binary_file(FLAGS_audio_file);
    if (audio_data.empty()) {
        discover_client.stop();
        return -1;
    }
    
    // 获取一个可用的服务地址
    std::string server_addr = get_available_service();
    if (server_addr.empty()) {
        std::cerr << "[Service Discovery] No available service" << std::endl;
        discover_client.stop();
        return -1;
    }
    std::cout << "\nSelected server: " << server_addr << std::endl;
    
    // ==================== 初始化 brpc 客户端通道 ====================
    brpc::Channel channel;
    brpc::ChannelOptions options;
    
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.connection_type = brpc::CONNECTION_TYPE_SHORT;
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = 3;
    
    if (channel.Init(server_addr.c_str(), "", &options) != 0) {
        std::cerr << "Failed to initialize channel to " << server_addr << std::endl;
        discover_client.stop();
        return -1;
    }
    
    speech::SpeechService_Stub stub(&channel);
    
    // ==================== 构造识别请求 ====================
    speech::SpeechRecognitionReq request;
    speech::SpeechRecognitionRsp response;
    brpc::Controller cntl;
    
    request.set_request_id(generate_request_id());
    request.set_speech_content(audio_data);
    request.set_user_id("test_user");
    request.set_session_id("test_session");
    
    std::cout << "\nSending recognition request, request_id: " << request.request_id() << std::endl;
    std::cout << "Audio data size: " << audio_data.size() << " bytes" << std::endl;
    
    // ==================== 发送 RPC 请求 ====================
    stub.SpeechRecognition(&cntl, &request, &response, nullptr);
    
    // ==================== 处理响应结果 ====================
    if (cntl.Failed()) {
        std::cerr << "RPC failed: " << cntl.ErrorText() << std::endl;
        discover_client.stop();
        return -1;
    }
    
    std::cout << "\n==== Recognition Result ====" << std::endl;
    std::cout << "request_id: " << response.request_id() << std::endl;
    std::cout << "success: " << (response.success() ? "true" : "false") << std::endl;
    
    if (response.success()) {
        std::cout << "recognition_result: " << response.recognition_result() << std::endl;
    } else {
        std::cout << "errmsg: " << response.errmsg() << std::endl;
    }
    
    std::cout << "RPC latency: " << cntl.latency_us() << " us" << std::endl;
    
    // ==================== 持续监控服务变化（后台运行） ====================
    std::cout << "\n==== Service Monitoring ====" << std::endl;
    std::cout << "Service discovery is running in background..." << std::endl;
    std::cout << "Press Ctrl+C to exit..." << std::endl;
    
    // 持续运行，监控服务变化
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 检查可用服务列表变化
        {
            std::lock_guard<std::mutex> lock(g_services_mutex);
            if (!g_available_services.empty()) {
                // 服务列表不为空，可以在这里添加负载均衡或故障转移逻辑
            }
        }
    }
    
    // 停止服务发现（实际上不会执行到这里，因为上面是死循环）
    discover_client.stop();
    return 0;
}