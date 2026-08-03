// =============================================================================
// friend_server.cc - 好友管理子服务主入口程序
// =============================================================================
// 本文件是好友管理子服务的启动入口，负责：
//   1. 解析命令行参数（gflags）
//   2. 通过 FriendServerBuilder 构建并初始化各组件
//   3. 依赖注入：将数据库、ES、通道池注入到 RPC 服务实现
//   4. 启动 brpc 服务器并注册到 etcd
//   5. 阻塞运行直到收到退出信号
//
// 启动命令示例：
//   ./friend_server --port=10005 --mysql_host=127.0.0.1 --etcd_addr=127.0.0.1
// =============================================================================

#include <gflags/gflags.h>

#include "friend_server_builder.hpp"
#include "friend_service_impl.hpp"

// ==================== gflags 命令行参数定义 ====================

// --- 网络配置 ---
DEFINE_int32(port, 10005, "TCP port of friend server");
DEFINE_string(listen_addr, "0.0.0.0", "Server listen address");
DEFINE_string(external_addr, "", "External access address for etcd registration");
DEFINE_int32(external_port, 0, "External access port for etcd registration");

// --- etcd 配置 ---
DEFINE_string(etcd_addr, "127.0.0.1", "Etcd server address");
DEFINE_int32(etcd_port, 2379, "Etcd server port");
DEFINE_int32(etcd_lease_ttl, 30, "Etcd lease TTL in seconds");

// --- MySQL 配置 ---
DEFINE_string(mysql_user, "root", "MySQL user name");
DEFINE_string(mysql_password, "123456", "MySQL password");
DEFINE_string(mysql_database, "chat_friend", "MySQL database name");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL server host");
DEFINE_int32(mysql_port, 0, "MySQL server port");

// --- Elasticsearch 配置 ---
DEFINE_string(es_host, "127.0.0.1", "Elasticsearch host");
DEFINE_int32(es_port, 9200, "Elasticsearch port");

// --- 日志配置 ---
DEFINE_bool(debug, false, "Run in debug mode");
DEFINE_string(log_file, "friend_server.log", "Log file path");
DEFINE_string(log_level, "INFO", "Log level: TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL");

/**
 * @brief 将日志级别字符串解析为枚举值
 * @param level 日志级别字符串
 * @return 对应的 LogLevel 枚举值，无法识别时默认返回 INFO
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

/**
 * @brief 主函数 - 好友管理服务入口
 *
 * 流程：
 *   1. 解析命令行参数
 *   2. 创建 RPC 服务实现对象
 *   3. 使用 Builder 模式构建并初始化各组件
 *   4. 依赖注入（数据库、ES、通道池）
 *   5. 创建 ES 索引
 *   6. 启动 brpc 服务器
 *   7. 向 etcd 注册服务
 *   8. 阻塞运行直到收到退出信号
 */
int main(int argc, char* argv[]) {
    gflags::SetUsageMessage("Friend Management Server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 创建 RPC 服务实现对象
    friend_service::FriendServiceImpl service_impl;

    // 使用 Builder 模式链式配置并构建各组件
    friend_service::FriendServerBuilder builder;
    bool success = builder
        .with_listen_address(FLAGS_listen_addr)
        .with_listen_port(FLAGS_port)
        .with_external_address(FLAGS_external_addr)
        .with_external_port(FLAGS_external_port)
        .with_etcd_address(FLAGS_etcd_addr)
        .with_etcd_port(FLAGS_etcd_port)
        .with_etcd_lease_ttl(FLAGS_etcd_lease_ttl)
        .with_mysql_user(FLAGS_mysql_user)
        .with_mysql_password(FLAGS_mysql_password)
        .with_mysql_database(FLAGS_mysql_database)
        .with_mysql_host(FLAGS_mysql_host)
        .with_mysql_port(FLAGS_mysql_port)
        .with_es_host(FLAGS_es_host)
        .with_es_port(FLAGS_es_port)
        .with_debug_mode(FLAGS_debug)
        .with_log_file(FLAGS_log_file)
        .with_log_level(parse_log_level(FLAGS_log_level))
        .register_brpc_service(&service_impl)
        .build();

    if (!success) {
        std::cerr << "Failed to build friend server" << std::endl;
        return -1;
    }

    // 依赖注入：将构建好的组件注入到 RPC 服务实现
    service_impl.set_friend_table(builder.get_friend_table());
    service_impl.set_friend_es(builder.get_friend_es());
    service_impl.set_channel_pool(builder.get_channel_pool());

    // 创建 Elasticsearch 用户索引（若不存在）
    auto es_client = builder.get_friend_es();
    if (es_client) {
        es_client->create_index();
    }

    // 启动 brpc 服务器，监听指定端口
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start friend server" << std::endl;
        return -1;
    }

    // 向 etcd 注册服务，使其他微服务能够发现本服务
    builder.register_service_to_etcd("friend_service");

    std::cout << "[FriendServer] Friend management server is running..." << std::endl;
    std::cout << "[FriendServer] Listening on: " << FLAGS_listen_addr << ":" << FLAGS_port << std::endl;

    // 阻塞运行，直到收到 SIGINT/SIGTERM 信号
    builder.get_brpc_server()->RunUntilAskedToQuit();

    // 优雅关闭各组件
    builder.stop();
    return 0;
}
