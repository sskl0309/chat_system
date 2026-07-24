// =============================================================================
// user_server.cc - 用户子服务主入口程序
// =============================================================================
// 本文件是用户子服务的主入口，负责：
//   1. 解析命令行参数（使用 gflags）
//   2. 创建用户服务实现对象
//   3. 使用建造者模式构建服务器（链式调用配置所有参数）
//   4. 设置各组件到服务实现
//   5. 启动 brpc 服务器
//   6. 向 etcd 注册服务
//   7. 阻塞等待退出信号
//   8. 释放资源并退出
//
// 编译：通过 CMakeLists.txt 中的 user_server 目标编译
// 运行：./user_server --port=10001 --mysql_user=root --mysql_password=123456
// =============================================================================

#include <gflags/gflags.h>

#include "user_server_builder.hpp"
#include "user_service_impl.hpp"

// ==================== gflags 命令行参数定义 ====================

/// 服务监听端口
DEFINE_int32(port, 10001, "TCP port of user server");

/// 服务监听地址
DEFINE_string(listen_addr, "0.0.0.0", "Server listen address");

/// 外部访问地址（用于 etcd 注册）
DEFINE_string(external_addr, "", "External access address for etcd registration");

/// 外部访问端口（用于 etcd 注册）
DEFINE_int32(external_port, 0, "External access port for etcd registration");

/// etcd 服务器地址
DEFINE_string(etcd_addr, "127.0.0.1", "Etcd server address");

/// etcd 服务器端口
DEFINE_int32(etcd_port, 2379, "Etcd server port");

/// etcd Lease 租约时长
DEFINE_int32(etcd_lease_ttl, 30, "Etcd lease TTL in seconds");

// MySQL 数据库配置
DEFINE_string(mysql_user, "root", "MySQL user name");
DEFINE_string(mysql_password, "123456", "MySQL password");
DEFINE_string(mysql_database, "chat_user", "MySQL database name");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL server host");
DEFINE_int32(mysql_port, 0, "MySQL server port (0 for default)");

// Redis 配置
DEFINE_string(redis_host, "127.0.0.1", "Redis server host");
DEFINE_int32(redis_port, 6379, "Redis server port");

// ES 配置
DEFINE_string(es_host, "127.0.0.1", "Elasticsearch server host");
DEFINE_int32(es_port, 9200, "Elasticsearch server port");

// 邮件配置
DEFINE_string(smtp_server, "smtp.qq.com", "SMTP server address");
DEFINE_int32(smtp_port, 587, "SMTP server port");
DEFINE_string(sender_email, "3502173090@qq.com", "Sender email address");
DEFINE_string(email_auth_code, "ilymtchfymaychdb", "SMTP auth code");

/// 调试模式开关
DEFINE_bool(debug, false, "Run in debug mode with console logging");

/// 日志文件路径
DEFINE_string(log_file, "user_server.log", "Log file path");

/// 日志输出级别
DEFINE_string(log_level, "INFO", "Log level: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL");

// ==================== 辅助函数 ====================

/**
 * @brief 将日志级别字符串转换为 mylog::LogLevel 枚举
 *
 * 支持 TRACE / DEBUG / INFO / WARN / ERROR / CRITICAL 六个级别，
 * 大小写敏感（需与 gflags 输入一致）。无法识别时默认返回 INFO。
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
    gflags::SetUsageMessage("User Management Server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 创建用户服务实现对象
    chat::UserServiceImpl service_impl;

    // 使用建造者模式构建服务器
    user_service::UserServerBuilder builder;
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
        .with_redis_host(FLAGS_redis_host)
        .with_redis_port(FLAGS_redis_port)
        .with_es_host(FLAGS_es_host)
        .with_es_port(FLAGS_es_port)
        .with_smtp_server(FLAGS_smtp_server)
        .with_smtp_port(FLAGS_smtp_port)
        .with_sender_email(FLAGS_sender_email)
        .with_email_auth_code(FLAGS_email_auth_code)
        .with_debug_mode(FLAGS_debug)
        .with_log_file(FLAGS_log_file)
        .with_log_level(parse_log_level(FLAGS_log_level))
        .register_brpc_service(&service_impl)
        .build();

    if (!success) {
        std::cerr << "Failed to build user server" << std::endl;
        return -1;
    }

    // 设置各组件到服务实现
    service_impl.set_user_table(builder.get_user_table());
    service_impl.set_redis_client(builder.get_redis_client());
    service_impl.set_email_client(builder.get_email_client());
    service_impl.set_user_es(builder.get_user_es());
    service_impl.set_channel_pool(builder.get_channel_pool());

    // 启动 brpc 服务器
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start user server" << std::endl;
        return -1;
    }

    // 向 etcd 注册服务
    success = builder.register_service_to_etcd("user_service");
    if (!success) {
        std::cerr << "Failed to register service to etcd" << std::endl;
    }

    // 阻塞等待退出信号
    builder.get_brpc_server()->RunUntilAskedToQuit();

    // 停止服务器
    builder.stop();
    return 0;
}
