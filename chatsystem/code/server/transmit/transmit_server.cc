// =============================================================================
// transmit_server.cc - 消息转发子服务主入口程序
// =============================================================================
// 本文件是消息转发子服务的主入口，负责：
//   1. 解析命令行参数（使用 gflags）
//   2. 创建消息转发服务实现对象
//   3. 使用建造者模式构建服务器（链式调用配置所有参数）
//   4. 设置各组件到服务实现
//   5. 启动 brpc 服务器
//   6. 向 etcd 注册服务
//   7. 阻塞等待退出信号
//   8. 释放资源并退出
//
// 服务功能概述：
//   消息转发子服务主要用于针对一条消息内容，组织消息的ID以及各项所需要素，
//   然后告诉网关服务器一条消息应该发给谁。通常消息都是以聊天会话为基础进行发送的，
//   根据会话找到它的所有成员，这就是转发的目标。除此之外，转发子服务将收到的消息，
//   放入消息队列中，由消息存储管理子服务进行消费存储。
//
// 编译：通过 CMakeLists.txt 中的 transmit_server 目标编译
// 运行：./transmit_server --port=10003 --mysql_user=root --mysql_password=123456
// =============================================================================

#include <gflags/gflags.h>

#include "transmit_server_builder.hpp"
#include "transmit_service_impl.hpp"

// ==================== gflags 命令行参数定义 ====================

/// 服务监听端口（消息转发服务默认 10003）
DEFINE_int32(port, 10003, "TCP port of transmit server");

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
DEFINE_string(mysql_database, "chat_friend", "MySQL database name");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL server host");
DEFINE_int32(mysql_port, 0, "MySQL server port (0 for default)");

// RabbitMQ 配置
DEFINE_string(mq_host, "127.0.0.1", "RabbitMQ server host");
DEFINE_int32(mq_port, 5672, "RabbitMQ server port");
DEFINE_string(mq_user, "guest", "RabbitMQ user name");
DEFINE_string(mq_password, "guest", "RabbitMQ password");
DEFINE_string(mq_vhost, "/", "RabbitMQ virtual host");

/// 调试模式开关
DEFINE_bool(debug, false, "Run in debug mode with console logging");

/// 日志文件路径
DEFINE_string(log_file, "transmit_server.log", "Log file path");

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

/**
 * @brief 消息转发服务主入口
 * 
 * 执行流程：
 *   1. 解析命令行参数
 *   2. 创建服务实现对象
 *   3. 使用建造者模式构建服务器（链式调用配置参数）
 *   4. 设置各组件到服务实现
 *   5. 启动 brpc 服务器
 *   6. 向 etcd 注册服务
 *   7. 阻塞等待退出信号
 *   8. 停止服务器并退出
 */
int main(int argc, char* argv[]) {
    // 设置命令行使用提示并解析参数
    gflags::SetUsageMessage("Message Transmit Server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 创建消息转发服务实现对象
    transmit::MsgTransmitServiceImpl service_impl;

    // 使用建造者模式构建服务器
    // 链式调用配置所有参数，最后调用 build() 完成构建
    transmit_service::TransmitServerBuilder builder;
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
        .with_mq_host(FLAGS_mq_host)
        .with_mq_port(FLAGS_mq_port)
        .with_mq_user(FLAGS_mq_user)
        .with_mq_password(FLAGS_mq_password)
        .with_mq_vhost(FLAGS_mq_vhost)
        .with_debug_mode(FLAGS_debug)
        .with_log_file(FLAGS_log_file)
        .with_log_level(parse_log_level(FLAGS_log_level))
        .register_brpc_service(&service_impl)
        .build();

    // 检查构建是否成功
    if (!success) {
        std::cerr << "Failed to build transmit server" << std::endl;
        return -1;
    }

    // 设置各组件到服务实现（依赖注入）
    service_impl.set_transmit_table(builder.get_transmit_table());
    service_impl.set_channel_pool(builder.get_channel_pool());
    service_impl.set_mq_client(builder.get_mq_client());

    // 启动 brpc 服务器
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start transmit server" << std::endl;
        return -1;
    }

    // 向 etcd 注册服务（服务名为 "transmit_service"）
    success = builder.register_service_to_etcd("transmit_service");
    if (!success) {
        std::cerr << "Failed to register service to etcd" << std::endl;
    }

    // 阻塞等待退出信号（Ctrl+C 或 kill 命令）
    // RunUntilAskedToQuit() 会一直阻塞直到收到退出信号
    builder.get_brpc_server()->RunUntilAskedToQuit();

    // 停止服务器（优雅关闭）
    builder.stop();

    // 正常退出
    return 0;
}
