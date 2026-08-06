// =============================================================================
// gateway_server.cc - 网关服务器主入口程序
// =============================================================================
// 本文件是网关服务器的主入口，负责：
//   1. 解析命令行参数（使用 gflags）
//   2. 使用建造者模式构建网关服务器（链式调用配置所有参数）
//   3. 启动网关服务器（HTTP + WebSocket）
//   4. 阻塞等待退出信号
//   5. 释放资源并退出
//
// 编译：通过 CMakeLists.txt 中的 gateway_server 目标编译
// 运行：./gateway_server --http_port=8888 --ws_port=8889 --redis_host=127.0.0.1
// =============================================================================

#include <gflags/gflags.h>
#include <csignal>
#include <atomic>

#include "gateway_server_builder.hpp"

// ==================== gflags 命令行参数定义 ====================

// HTTP 服务配置
DEFINE_int32(http_port, 8888, "HTTP service port for business requests");
DEFINE_string(http_listen_addr, "0.0.0.0", "HTTP server listen address");

// WebSocket 服务配置
DEFINE_int32(ws_port, 8889, "WebSocket service port for event notifications");
DEFINE_string(ws_listen_addr, "0.0.0.0", "WebSocket server listen address");

// etcd 配置（用于服务发现）
DEFINE_string(etcd_addr, "127.0.0.1", "Etcd server address for service discovery");
DEFINE_int32(etcd_port, 2379, "Etcd server port");

// Redis 配置（用于会话鉴权）
DEFINE_string(redis_host, "127.0.0.1", "Redis server host for session management");
DEFINE_int32(redis_port, 6379, "Redis server port");
DEFINE_int32(redis_db, 0, "Redis database number");

// 日志配置
DEFINE_bool(debug, false, "Run in debug mode with console logging");
DEFINE_string(log_file, "gateway_server.log", "Log file path");
DEFINE_string(log_level, "INFO", "Log level: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL");

// ==================== 全局信号处理 ====================

static std::atomic<bool> g_running(true);

/**
 * @brief 信号处理函数
 *
 * 捕获 SIGINT 和 SIGTERM 信号，优雅关闭服务器。
 *
 * @param signal 信号编号
 */
void signal_handler(int signal) {
    LOG_INFO("[GatewayServer] Received signal: {}, shutting down...", signal);
    g_running = false;
}

// ==================== 辅助函数 ====================

/**
 * @brief 将日志级别字符串转换为 mylog::LogLevel 枚举
 *
 * 支持 TRACE / DEBUG / INFO / WARN / ERROR / CRITICAL 六个级别。
 *
 * @param level 日志级别字符串（如 "DEBUG"）
 * @return 对应的 LogLevel 枚举值
 */
mylog::LogLevel parse_log_level(const std::string& level) {
    if (level == "TRACE") return mylog::LogLevel::TRACE;
    if (level == "DEBUG") return mylog::LogLevel::DEBUG;
    if (level == "INFO") return mylog::LogLevel::INFO;
    if (level == "WARN") return mylog::LogLevel::WARN;
    if (level == "ERROR") return mylog::LogLevel::ERROR;
    if (level == "CRITICAL") return mylog::LogLevel::CRITICAL;
    return mylog::LogLevel::INFO;
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::SetUsageMessage("Chat System Gateway Server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    LOG_INFO("[GatewayServer] Starting gateway server...");

    // 使用建造者模式构建服务器
    gateway::GatewayServerBuilder builder;
    bool success = builder
        .with_http_listen_address(FLAGS_http_listen_addr)
        .with_http_port(FLAGS_http_port)
        .with_ws_listen_address(FLAGS_ws_listen_addr)
        .with_ws_port(FLAGS_ws_port)
        .with_etcd_address(FLAGS_etcd_addr)
        .with_etcd_port(FLAGS_etcd_port)
        .with_redis_host(FLAGS_redis_host)
        .with_redis_port(FLAGS_redis_port)
        .with_redis_db(FLAGS_redis_db)
        .with_debug_mode(FLAGS_debug)
        .with_log_file(FLAGS_log_file)
        .with_log_level(parse_log_level(FLAGS_log_level))
        .build();

    if (!success) {
        std::cerr << "Failed to build gateway server" << std::endl;
        return -1;
    }

    // 启动服务器
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start gateway server" << std::endl;
        return -1;
    }

    LOG_INFO("[GatewayServer] Gateway server is running. HTTP: {}, WS: {}",
             FLAGS_http_port, FLAGS_ws_port);

    // 阻塞等待退出信号
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 停止服务器
    LOG_INFO("[GatewayServer] Stopping gateway server...");
    builder.stop();

    LOG_INFO("[GatewayServer] Gateway server stopped.");
    return 0;
}
