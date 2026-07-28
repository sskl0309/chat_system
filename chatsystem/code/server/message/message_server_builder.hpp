// =============================================================================
// message_server_builder.hpp - 消息存储服务器建造者类声明（建造者模式）
// =============================================================================
// 本头文件声明 MessageServerBuilder 类，采用建造者模式统一管理消息存储子服务的
// 各组件构造过程，包括日志、数据库、ES、etcd 客户端、brpc 服务、RPC 信道池、MQ 客户端。
//
// 设计模式：建造者模式（Builder Pattern）
//   - 通过链式调用（Fluent API）配置服务器参数
//   - 将复杂对象的构造过程与表示分离
//   - 支持灵活配置，所有组件均可按需初始化
//
// 初始化顺序（build 方法中依次执行）：
//   1. Logger       - 日志系统
//   2. Database     - MySQL 数据库连接
//   3. Elasticsearch - ES 客户端
//   4. MQ Client    - RabbitMQ 客户端
//   5. Channel Pool - RPC 信道池（etcd 服务发现）
//   6. BRPC Server  - brpc RPC 服务器
//   7. Etcd Client  - etcd 服务注册客户端
// =============================================================================

#ifndef MESSAGE_SERVER_BUILDER_HPP
#define MESSAGE_SERVER_BUILDER_HPP

#include <string>
#include <memory>
#include <vector>
#include <atomic>

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <odb/database.hxx>

#include "../common/log.hpp"
#include "../common/etcd_client.hpp"
#include "../common/brpc_client.hpp"
#include "../common/mq_client.hpp"
#include "message_table.hpp"
#include "message_es.hpp"

namespace message_service {

/**
 * @brief 消息存储服务器配置结构体
 *
 * 聚合所有组件的配置参数，便于统一传递和管理。
 */
struct MessageServerConfig {
    // 网络配置
    std::string listen_addr;       // 服务监听地址
    int listen_port;               // 服务监听端口
    std::string external_addr;     // 对外暴露地址（etcd 注册用）
    int external_port;             // 对外暴露端口

    // etcd 配置
    std::string etcd_addr;         // etcd 服务器地址
    int etcd_port;                 // etcd 服务器端口

    // MySQL 数据库配置
    std::string mysql_user;        // 数据库用户名
    std::string mysql_password;    // 数据库密码
    std::string mysql_database;    // 数据库名
    std::string mysql_host;        // 数据库主机
    int mysql_port;                // 数据库端口

    // Elasticsearch 配置
    std::string es_host;           // ES 主机地址
    int es_port;                   // ES 端口

    // RabbitMQ 配置
    std::string mq_host;           // MQ 主机地址
    int mq_port;                   // MQ 端口
    std::string mq_user;           // MQ 用户名
    std::string mq_password;       // MQ 密码
    std::string mq_vhost;          // MQ 虚拟主机

    // 日志配置
    bool is_debug;                 // 是否调试模式
    std::string log_file;          // 日志文件路径
};

/**
 * @brief 消息存储服务器建造者类
 *
 * 使用示例：
 *   MessageServerBuilder builder;
 *   builder.with_listen_port(10004)
 *          .with_mysql_host("127.0.0.1")
 *          .with_mysql_port(3306)
 *          .register_brpc_service(&service_impl)
 *          .build();
 *   builder.start();
 */
class MessageServerBuilder {
public:
    MessageServerBuilder();
    ~MessageServerBuilder();

    // ==================== 配置方法（链式调用，返回自身引用） ====================

    /**
     * @brief 设置服务监听地址
     * @param addr IP 地址（默认 0.0.0.0）
     */
    MessageServerBuilder& with_listen_address(const std::string& addr);

    /**
     * @brief 设置服务监听端口
     * @param port 端口号（默认 10004）
     */
    MessageServerBuilder& with_listen_port(int port);

    /**
     * @brief 设置对外暴露地址
     * @param addr IP 地址（用于 etcd 服务注册）
     */
    MessageServerBuilder& with_external_address(const std::string& addr);

    /**
     * @brief 设置对外暴露端口
     * @param port 端口号（用于 etcd 服务注册）
     */
    MessageServerBuilder& with_external_port(int port);

    // ==================== etcd 配置 ====================

    /**
     * @brief 设置 etcd 服务器地址
     * @param addr IP 地址
     */
    MessageServerBuilder& with_etcd_address(const std::string& addr);

    /**
     * @brief 设置 etcd 服务器端口
     * @param port 端口号（默认 2379）
     */
    MessageServerBuilder& with_etcd_port(int port);

    /**
     * @brief 设置 etcd 租约 TTL
     * @param ttl 秒数（默认 30）
     */
    MessageServerBuilder& with_etcd_lease_ttl(int ttl);

    // ==================== MySQL 配置 ====================

    /**
     * @brief 设置 MySQL 用户名
     */
    MessageServerBuilder& with_mysql_user(const std::string& user);

    /**
     * @brief 设置 MySQL 密码
     */
    MessageServerBuilder& with_mysql_password(const std::string& password);

    /**
     * @brief 设置 MySQL 数据库名
     */
    MessageServerBuilder& with_mysql_database(const std::string& database);

    /**
     * @brief 设置 MySQL 主机地址
     */
    MessageServerBuilder& with_mysql_host(const std::string& host);

    /**
     * @brief 设置 MySQL 端口
     */
    MessageServerBuilder& with_mysql_port(int port);

    // ==================== Elasticsearch 配置 ====================

    /**
     * @brief 设置 ES 主机地址
     */
    MessageServerBuilder& with_es_host(const std::string& host);

    /**
     * @brief 设置 ES 端口
     */
    MessageServerBuilder& with_es_port(int port);

    // ==================== RabbitMQ 配置 ====================

    /**
     * @brief 设置 MQ 主机地址
     */
    MessageServerBuilder& with_mq_host(const std::string& host);

    /**
     * @brief 设置 MQ 端口
     */
    MessageServerBuilder& with_mq_port(int port);

    /**
     * @brief 设置 MQ 用户名
     */
    MessageServerBuilder& with_mq_user(const std::string& user);

    /**
     * @brief 设置 MQ 密码
     */
    MessageServerBuilder& with_mq_password(const std::string& password);

    /**
     * @brief 设置 MQ 虚拟主机
     */
    MessageServerBuilder& with_mq_vhost(const std::string& vhost);

    // ==================== 日志配置 ====================

    /**
     * @brief 设置调试模式
     * @param debug true 启用控制台日志
     */
    MessageServerBuilder& with_debug_mode(bool debug);

    /**
     * @brief 设置日志文件路径
     * @param file 文件路径
     */
    MessageServerBuilder& with_log_file(const std::string& file);

    /**
     * @brief 设置日志级别
     * @param mylog::LogLevel 日志级别
     */
    MessageServerBuilder& with_log_level(mylog::LogLevel level);

    // ==================== 服务注册与构建 ====================

    /**
     * @brief 注册 protobuf RPC 服务
     *
     * 可多次调用注册多个服务（如 GetRecentMsg、MsgSearch 等）
     * @param service 服务实例指针
     */
    MessageServerBuilder& register_brpc_service(google::protobuf::Service* service);

    /**
     * @brief 按顺序构建所有组件
     *
     * 依次初始化：日志 → 数据库 → ES → MQ → 信道池 → brpc → etcd
     * @return true 全部初始化成功
     */
    bool build();

    // ==================== 访问器方法（用于获取已构建的组件） ====================

    std::shared_ptr<message_table::MessageTable> get_message_table() const;
    std::shared_ptr<message_es::MessageES> get_message_es() const;
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const;
    std::shared_ptr<mq::MQClient> get_mq_client() const;
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client();
    brpc::Server* get_brpc_server();

    // ==================== 运行时方法 ====================

    /**
     * @brief 向 etcd 注册服务
     * @param service_name 服务名称（如 "message_storage_service"）
     */
    bool register_service_to_etcd(const std::string& service_name);

    /**
     * @brief 启动 brpc RPC 服务器
     * @return true 启动成功
     */
    bool start();

    /**
     * @brief 停止服务器并释放所有资源
     */
    void stop();

private:
    // ==================== 私有初始化方法 ====================
    bool init_logger();        // 初始化日志系统
    bool init_database();      // 初始化 MySQL 数据库
    bool init_es();            // 初始化 Elasticsearch 客户端
    bool init_mq_client();     // 初始化 RabbitMQ 客户端
    bool init_channel_pool();  // 初始化 RPC 信道池
    bool init_brpc_server();   // 初始化 brpc RPC 服务器
    bool init_etcd_client();   // 初始化 etcd 客户端

    // ==================== 成员变量 ====================

    MessageServerConfig config_;                    // 服务器配置
    int etcd_lease_ttl_;                             // etcd 租约 TTL
    mylog::LogLevel log_level_;                      // 日志级别

    // 已构建的组件
    std::shared_ptr<message_table::MessageTable> message_table_;     // 数据库操作
    std::shared_ptr<message_es::MessageES> message_es_;             // ES 客户端
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;         // RPC 信道池
    std::shared_ptr<mq::MQClient> mq_client_;                       // MQ 客户端

    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;      // etcd 客户端
    std::unique_ptr<brpc::Server> brpc_server_;                      // brpc 服务器
    std::atomic<bool> running_;                                      // 运行状态标志

    // 待注册的 RPC 服务列表
    std::vector<google::protobuf::Service*> registered_services_;
};

} // namespace message_service

#endif // MESSAGE_SERVER_BUILDER_HPP