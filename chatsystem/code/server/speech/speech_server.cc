// 语音识别服务主程序
// 负责启动语音识别 RPC 服务器，处理客户端的语音识别请求
// 使用 brpc 框架作为 RPC 通信层，etcd 作为服务注册中心

// gflags: brpc 使用的命令行参数解析库
#include <gflags/gflags.h>
// brpc 服务端头文件
#include <brpc/server.h>

// 语音识别服务实现类
#include "speech_service_impl.h"
// 日志模块
#include "../common/log.hpp"
// etcd 服务注册客户端
#include "../common/etcd_client.hpp"

// ==================== RPC 服务器参数 ====================
DEFINE_int32(port, 8000, "TCP port of this server");           // RPC 服务器监听端口
DEFINE_string(listen_addr, "", "Server listen address, default is 0.0.0.0");  // RPC 服务器监听地址

// ==================== 服务注册中心参数 ====================
DEFINE_string(etcd_addr, "http://localhost:2379", "Etcd server address for service registration");  // etcd 服务器地址
DEFINE_string(service_name, "speech_service", "Service name for registration");                      // 服务名称
DEFINE_string(external_addr, "", "External access address for service registration, default is listen_addr:port");  // 外部访问地址

// ==================== 语音识别平台参数 ====================
DEFINE_string(app_id, "", "Baidu AI speech AppID");           // 百度AI开放平台应用ID
DEFINE_string(api_key, "", "Baidu AI speech API Key");       // API Key，用于获取访问令牌
DEFINE_string(secret_key, "", "Baidu AI speech Secret Key"); // Secret Key，用于获取访问令牌

// ==================== 日志模块参数 ====================
DEFINE_bool(is_debug, false, "Run in debug mode, log to console");      // 是否运行在调试模式
DEFINE_string(log_file, "speech_server.log", "Log file name when not in debug mode");  // 日志文件名
DEFINE_int32(log_level, 2, "Log level: 0-Trace, 1-Debug, 2-Info, 3-Warn, 4-Error, 5-Critical");  // 日志级别

/**
 * @brief 语音识别服务主函数
 * 
 * 服务器搭建流程：
 * 1. 参数解析 -- 基于 gflags 模块，解析命令行参数
 * 2. 初始化日志模块 -- 根据运行模式初始化日志输出
 * 3. 搭建 RPC 服务器 -- 实现语音识别业务接口功能
 * 4. 向注册中心进行服务注册 -- 将服务信息注册到 etcd
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出码，0表示成功，非0表示失败
 */
int main(int argc, char* argv[]) {
    // 设置命令行帮助信息
    gflags::SetUsageMessage("Speech Recognition Service");
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 将命令行传入的日志级别转换为 mylog::LogLevel 枚举
    mylog::LogLevel log_level;
    switch (FLAGS_log_level) {
        case 0: log_level = mylog::LogLevel::TRACE; break;
        case 1: log_level = mylog::LogLevel::DEBUG; break;
        case 2: log_level = mylog::LogLevel::INFO; break;
        case 3: log_level = mylog::LogLevel::WARN; break;
        case 4: log_level = mylog::LogLevel::ERROR; break;
        case 5: log_level = mylog::LogLevel::CRITICAL; break;
        default: log_level = mylog::LogLevel::INFO; break;
    }

    // 初始化日志模块：调试模式输出到控制台，否则输出到文件
    mylog::init(FLAGS_is_debug, FLAGS_log_file, log_level);

    // 校验语音识别平台认证信息是否完整（必填项）
    if (FLAGS_app_id.empty() || FLAGS_api_key.empty() || FLAGS_secret_key.empty()) {
        LOG_ERROR("[speech_server] Please provide app_id, api_key and secret_key");
        return -1;
    }

    // 输出服务启动信息和配置参数
    LOG_INFO("[speech_server] Starting speech recognition service...");
    LOG_INFO("[speech_server] Configuration:");
    LOG_INFO("[speech_server]   port={}, listen_addr={}", FLAGS_port, FLAGS_listen_addr);
    LOG_INFO("[speech_server]   etcd_addr={}, service_name={}", FLAGS_etcd_addr, FLAGS_service_name);
    LOG_INFO("[speech_server]   external_addr={}", FLAGS_external_addr);
    LOG_INFO("[speech_server]   is_debug={}, log_file={}", FLAGS_is_debug, FLAGS_log_file);

    // 创建 brpc 服务器实例
    brpc::Server server;

    // 创建语音识别服务实现对象，传入百度AI认证信息
    speech::SpeechServiceImpl speech_service(FLAGS_app_id, FLAGS_api_key, FLAGS_secret_key);

    // 将服务注册到 brpc 服务器
    // SERVER_DOESNT_OWN_SERVICE 表示 server 不负责释放 service 对象
    if (server.AddService(&speech_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR("[speech_server] Failed to add speech service");
        return -1;
    }

    // 构建监听地址：listen_addr 为空则使用 0.0.0.0:port
    std::string listen_address = FLAGS_listen_addr.empty()
                                 ? std::string("0.0.0.0:") + std::to_string(FLAGS_port)
                                 : FLAGS_listen_addr;

    // 构建服务注册地址：external_addr 为空则使用监听地址，否则使用外部访问地址
    // （云服务器绑定与访问地址不一致时需要配置 external_addr）
    std::string register_address = FLAGS_external_addr.empty()
                                   ? listen_address
                                   : FLAGS_external_addr;

    // 服务器配置选项
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;  // 不启用空闲连接超时

    // 启动 brpc 服务器
    if (server.Start(listen_address.c_str(), &options) != 0) {
        LOG_ERROR("[speech_server] Failed to start server on {}", listen_address);
        return -1;
    }

    LOG_INFO("[speech_server] Speech server is listening on {}", listen_address);

    // 创建 etcd 服务注册客户端，租约时长30秒
    etcd::ServiceRegisterClient register_client(FLAGS_etcd_addr, 30);
    // 向 etcd 注册服务，注册路径格式：/services/{service_name}/{host_address}
    bool registered = register_client.register_service(FLAGS_service_name, register_address);

    if (registered) {
        LOG_INFO("[speech_server] Registered service to etcd: {} -> {}", 
                 FLAGS_service_name, register_address);
    } else {
        LOG_WARN("[speech_server] Failed to register service to etcd, server will continue running");
    }

    // 阻塞等待直到收到退出信号（Ctrl+C）
    server.RunUntilAskedToQuit();

    // 停止服务注册，释放 etcd 连接和 Lease 资源
    register_client.stop();
    LOG_INFO("[speech_server] Speech recognition service stopped");

    return 0;
}
