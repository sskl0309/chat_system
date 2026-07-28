// =============================================================================
// message_server.cc - 消息存储子服务主入口程序
// =============================================================================
// 本文件是消息存储子服务的主入口，负责整个服务的启动流程：
//   1. 解析命令行参数（使用 gflags）
//   2. 创建消息存储服务实现对象
//   3. 使用建造者模式构建服务器（链式调用配置所有参数）
//   4. 设置各组件到服务实现
//   5. 创建 ES 索引（如果不存在）
//   6. 启动 MQ 消息消费
//   7. 启动 brpc 服务器
//   8. 向 etcd 注册服务
//   9. 阻塞等待退出信号
//   10. 释放资源并退出
//
// 服务功能概述：
//   消息存储子服务负责：
//   1. 从 MQ 消费消息，进行持久化存储（MySQL + ES + 文件服务）
//   2. 对外提供消息获取接口：最近N条消息、指定时间段消息、关键字搜索
//
// 编译：通过 CMakeLists.txt 中的 message_server 目标编译
// 运行：./message_server --port=10004 --mysql_user=root --mysql_password=123456
// =============================================================================

#include <gflags/gflags.h>

#include "message_server_builder.hpp"
#include "message_service_impl.hpp"

// ==================== gflags 命令行参数定义 ====================
// 所有参数均有默认值，可通过命令行覆盖

// 网络配置
DEFINE_int32(port, 10004, "TCP port of message storage server");
DEFINE_string(listen_addr, "0.0.0.0", "Server listen address");
DEFINE_string(external_addr, "", "External access address for etcd registration");
DEFINE_int32(external_port, 0, "External access port for etcd registration");

// etcd 配置
DEFINE_string(etcd_addr, "127.0.0.1", "Etcd server address");
DEFINE_int32(etcd_port, 2379, "Etcd server port");
DEFINE_int32(etcd_lease_ttl, 30, "Etcd lease TTL in seconds");

// MySQL 数据库配置
DEFINE_string(mysql_user, "root", "MySQL user name");
DEFINE_string(mysql_password, "123456", "MySQL password");
DEFINE_string(mysql_database, "chat_message", "MySQL database name");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL server host");
DEFINE_int32(mysql_port, 0, "MySQL server port (0 for default)");

// Elasticsearch 配置
DEFINE_string(es_host, "127.0.0.1", "Elasticsearch host");
DEFINE_int32(es_port, 9200, "Elasticsearch port");

// RabbitMQ 配置
DEFINE_string(mq_host, "127.0.0.1", "RabbitMQ server host");
DEFINE_int32(mq_port, 5672, "RabbitMQ server port");
DEFINE_string(mq_user, "guest", "RabbitMQ user name");
DEFINE_string(mq_password, "guest", "RabbitMQ password");
DEFINE_string(mq_vhost, "/", "RabbitMQ virtual host");

// 日志配置
DEFINE_bool(debug, false, "Run in debug mode with console logging");
DEFINE_string(log_file, "message_server.log", "Log file path");
DEFINE_string(log_level, "INFO", "Log level: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL");

// ==================== 辅助函数 ====================

/**
 * @brief 解析日志级别字符串为枚举值
 * @param level 日志级别字符串
 * @return mylog::LogLevel 枚举值
 */
mylog::LogLevel parse_log_level(const std::string& level) {
    if (level == "TRACE") return mylog::LogLevel::TRACE;
    if (level == "DEBUG") return mylog::LogLevel::DEBUG;
    if (level == "INFO") return mylog::LogLevel::INFO;
    if (level == "WARN") return mylog::LogLevel::WARN;
    if (level == "ERROR") return mylog::LogLevel::ERROR;
    if (level == "CRITICAL") return mylog::LogLevel::CRITICAL;
    return mylog::LogLevel::INFO;  // 默认 INFO 级别
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::SetUsageMessage("Message Storage Server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // Step 1: 创建消息存储服务实现对象
    message::MsgStorageServiceImpl service_impl;

    // Step 2: 使用建造者模式构建服务器
    // 通过链式调用设置所有配置参数
    message_service::MessageServerBuilder builder;
    bool success = builder
        // 网络配置
        .with_listen_address(FLAGS_listen_addr)
        .with_listen_port(FLAGS_port)
        .with_external_address(FLAGS_external_addr)
        .with_external_port(FLAGS_external_port)
        // etcd 配置
        .with_etcd_address(FLAGS_etcd_addr)
        .with_etcd_port(FLAGS_etcd_port)
        .with_etcd_lease_ttl(FLAGS_etcd_lease_ttl)
        // MySQL 配置
        .with_mysql_user(FLAGS_mysql_user)
        .with_mysql_password(FLAGS_mysql_password)
        .with_mysql_database(FLAGS_mysql_database)
        .with_mysql_host(FLAGS_mysql_host)
        .with_mysql_port(FLAGS_mysql_port)
        // ES 配置
        .with_es_host(FLAGS_es_host)
        .with_es_port(FLAGS_es_port)
        // RabbitMQ 配置
        .with_mq_host(FLAGS_mq_host)
        .with_mq_port(FLAGS_mq_port)
        .with_mq_user(FLAGS_mq_user)
        .with_mq_password(FLAGS_mq_password)
        .with_mq_vhost(FLAGS_mq_vhost)
        // 日志配置
        .with_debug_mode(FLAGS_debug)
        .with_log_file(FLAGS_log_file)
        .with_log_level(parse_log_level(FLAGS_log_level))
        // 注册 RPC 服务实现
        .register_brpc_service(&service_impl)
        // 构建所有组件
        .build();

    if (!success) {
        std::cerr << "Failed to build message storage server" << std::endl;
        return -1;
    }

    // Step 3: 将构建好的组件注入到服务实现中
    // 依赖注入，使服务实现可以访问数据库、ES、RPC信道池、MQ等
    service_impl.set_message_table(builder.get_message_table());
    service_impl.set_message_es(builder.get_message_es());
    service_impl.set_channel_pool(builder.get_channel_pool());
    service_impl.set_mq_client(builder.get_mq_client());

    // Step 4: 创建 ES 索引（如果不存在）
    auto es_client = builder.get_message_es();
    if (es_client) {
        es_client->create_index();
    }

    // Step 5: 启动 MQ 消息消费
    // 订阅 message_storage_queue 队列，使用手动确认模式
    auto mq_client = builder.get_mq_client();
    if (mq_client) {
        mq_client->consume("message_storage_queue",
            [&service_impl](const std::string& message, uint64_t deliveryTag) {
                service_impl.on_message_consume(message, deliveryTag);
            },
            false  // false = 手动确认模式（需要显式 ack/reject）
        );
        std::cout << "[MessageServer] MQ consumer started, waiting for messages..." << std::endl;
    }

    // Step 6: 启动 brpc RPC 服务器
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start message storage server" << std::endl;
        return -1;
    }

    // Step 7: 向 etcd 注册服务
    // 注册服务名为 "message_storage_service"，供其他微服务发现
    success = builder.register_service_to_etcd("message_storage_service");
    if (!success) {
        std::cerr << "Failed to register service to etcd" << std::endl;
        // 服务注册失败不影响运行，仅记录错误
    }

    // 打印启动信息
    std::cout << "[MessageServer] Message storage server is running..." << std::endl;
    std::cout << "[MessageServer] Listening on: " << FLAGS_listen_addr << ":" << FLAGS_port << std::endl;

    // Step 8: 阻塞等待退出信号
    // RunUntilAskedToQuit 会阻塞直到收到 SIGINT/SIGTERM 信号
    builder.get_brpc_server()->RunUntilAskedToQuit();

    // Step 9: 停止服务器并释放资源
    builder.stop();

    return 0;
}