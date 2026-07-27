// =============================================================================
// transmit_server_builder.hpp - 消息转发服务器建造者类声明（建造者模式）
// =============================================================================
// 本头文件声明 TransmitServerBuilder 类，采用建造者模式统一管理消息转发子服务的
// 各组件构造过程，包括日志、数据库、etcd 客户端、brpc 服务、RPC 信道池、MQ 客户端。
//
// 设计模式：建造者模式（Builder Pattern）
//   - 通过链式调用（Fluent API）配置服务器参数，代码清晰简洁
//   - 将复杂对象的构造过程与表示分离，便于扩展配置项
//
// 依赖组件：
//   - 日志系统：基于 spdlog 封装
//   - 数据库：MySQL + ODB ORM 框架
//   - etcd：服务注册与发现
//   - brpc：RPC 框架
//   - MQ：RabbitMQ 消息队列
// =============================================================================

#ifndef TRANSMIT_SERVER_BUILDER_HPP
#define TRANSMIT_SERVER_BUILDER_HPP

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
#include "transmit_table.hpp"

namespace transmit_service {

/**
 * @brief 消息转发服务器配置结构体
 * 
 * 包含服务运行所需的所有配置参数，通过 gflags 命令行参数填充。
 */
struct TransmitServerConfig {
    // 网络配置
    std::string listen_addr;        ///< 服务监听地址
    int listen_port;                ///< 服务监听端口
    std::string external_addr;      ///< 外部访问地址（用于etcd注册）
    int external_port;              ///< 外部访问端口

    // etcd 配置
    std::string etcd_addr;          ///< etcd 服务器地址
    int etcd_port;                  ///< etcd 服务器端口

    // MySQL 数据库配置
    std::string mysql_user;         ///< MySQL 用户名
    std::string mysql_password;     ///< MySQL 密码
    std::string mysql_database;     ///< MySQL 数据库名
    std::string mysql_host;         ///< MySQL 主机地址
    int mysql_port;                 ///< MySQL 端口

    // RabbitMQ 配置
    std::string mq_host;            ///< RabbitMQ 服务器地址
    int mq_port;                    ///< RabbitMQ 服务器端口
    std::string mq_user;            ///< RabbitMQ 用户名
    std::string mq_password;        ///< RabbitMQ 密码
    std::string mq_vhost;           ///< RabbitMQ 虚拟主机

    // 日志配置
    bool is_debug;                  ///< 是否调试模式
    std::string log_file;           ///< 日志文件路径
};

/**
 * @brief 消息转发服务器建造者类
 * 
 * 采用建造者模式，统一管理 brpc 服务、数据库、etcd 客户端、
 * RPC 信道池、MQ 客户端的构造过程。
 */
class TransmitServerBuilder {
public:
    /**
     * @brief 默认构造函数
     * 
     * 初始化默认配置参数。
     */
    TransmitServerBuilder();

    /**
     * @brief 析构函数
     * 
     * 自动调用 stop() 停止所有组件，释放资源。
     */
    ~TransmitServerBuilder();

    // ==================== 配置方法（链式调用） ====================
    // 每个 with_xxx 方法设置一项配置并返回 *this，支持链式调用
    // 示例：builder.with_listen_port(8080).with_mysql_user("root").build();

    /// 设置服务监听地址
    TransmitServerBuilder& with_listen_address(const std::string& addr);

    /// 设置服务监听端口
    TransmitServerBuilder& with_listen_port(int port);

    /// 设置对外暴露地址（用于 etcd 服务注册，通常为公网IP）
    TransmitServerBuilder& with_external_address(const std::string& addr);

    /// 设置对外暴露端口
    TransmitServerBuilder& with_external_port(int port);

    /// 设置 etcd 服务器地址
    TransmitServerBuilder& with_etcd_address(const std::string& addr);

    /// 设置 etcd 服务器端口
    TransmitServerBuilder& with_etcd_port(int port);

    /// 设置 etcd 租约 TTL（秒），到期后自动注销服务
    TransmitServerBuilder& with_etcd_lease_ttl(int ttl);

    /// 设置 MySQL 用户名
    TransmitServerBuilder& with_mysql_user(const std::string& user);

    /// 设置 MySQL 密码
    TransmitServerBuilder& with_mysql_password(const std::string& password);

    /// 设置 MySQL 数据库名
    TransmitServerBuilder& with_mysql_database(const std::string& database);

    /// 设置 MySQL 主机地址
    TransmitServerBuilder& with_mysql_host(const std::string& host);

    /// 设置 MySQL 端口（0 表示使用默认端口 3306）
    TransmitServerBuilder& with_mysql_port(int port);

    /// 设置 RabbitMQ 服务器地址
    TransmitServerBuilder& with_mq_host(const std::string& host);

    /// 设置 RabbitMQ 服务器端口
    TransmitServerBuilder& with_mq_port(int port);

    /// 设置 RabbitMQ 用户名
    TransmitServerBuilder& with_mq_user(const std::string& user);

    /// 设置 RabbitMQ 密码
    TransmitServerBuilder& with_mq_password(const std::string& password);

    /// 设置 RabbitMQ 虚拟主机
    TransmitServerBuilder& with_mq_vhost(const std::string& vhost);

    /// 设置调试模式（控制台彩色输出 vs 文件输出）
    TransmitServerBuilder& with_debug_mode(bool debug);

    /// 设置日志文件路径
    TransmitServerBuilder& with_log_file(const std::string& file);

    /// 设置日志级别
    TransmitServerBuilder& with_log_level(mylog::LogLevel level);

    // ==================== 服务注册与构建 ====================

    /**
     * @brief 注册 brpc 服务实现
     * 
     * 允许多次调用注册多个 service，最终一起绑定到 brpc::Server。
     * 注意：传入的 service 指针生命周期需长于 brpc::Server。
     * 
     * @param service brpc 服务实现指针
     * @return 建造者实例引用（链式调用）
     */
    TransmitServerBuilder& register_brpc_service(google::protobuf::Service* service);

    /**
     * @brief 构建所有组件
     * 
     * 按顺序初始化：日志、数据库、MQ 客户端、RPC 信道池、brpc服务器、etcd客户端
     * 
     * @return 构建成功返回 true，失败返回 false
     */
    bool build();

    // ==================== 访问器方法 ====================

    /**
     * @brief 获取数据库操作实例
     * @return TransmitTable 智能指针
     */
    std::shared_ptr<transmit_table::TransmitTable> get_transmit_table() const;

    /**
     * @brief 获取 RPC 信道池
     * @return ServiceChannelPool 智能指针
     */
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const;

    /**
     * @brief 获取 MQ 客户端
     * @return MQClient 智能指针
     */
    std::shared_ptr<mq::MQClient> get_mq_client() const;

    /**
     * @brief 获取 etcd 服务注册客户端
     * @return ServiceRegisterClient 智能指针引用
     */
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client();

    /**
     * @brief 获取 brpc 服务器实例
     * @return brpc::Server 指针
     */
    brpc::Server* get_brpc_server();

    // ==================== 运行时方法 ====================

    /**
     * @brief 向 etcd 注册当前服务
     * 
     * 向 etcd 写入 /services/{service_name}/{host_address}，
     * 并启动租约保活线程，TTL 到期后自动移除。
     * 
     * @param service_name 服务名称（如 "transmit_service"）
     * @return 注册成功返回 true
     */
    bool register_service_to_etcd(const std::string& service_name);

    /**
     * @brief 启动 brpc 服务器
     * 
     * 在 listen_addr:listen_port 上启动服务，设置 idle_timeout_sec=-1
     * 表示永不因空闲断开连接。
     * 
     * @return 启动成功返回 true
     */
    bool start();

    /**
     * @brief 停止所有组件
     * 
     * 按顺序停止：etcd 保活 → MQ 客户端 → 信道池 → brpc 服务器（优雅退出 + Join 等待）
     */
    void stop();

private:
    // ==================== 组件初始化方法 ====================
    // 每个 init_xxx 负责初始化一个子系统，失败返回 false 将中断整个 build() 流程

    /**
     * @brief 初始化日志系统
     * @return 成功返回 true，失败返回 false
     */
    bool init_logger();

    /**
     * @brief 初始化 MySQL 数据库连接，创建 TransmitTable 实例
     * @return 成功返回 true，失败返回 false
     */
    bool init_database();

    /**
     * @brief 初始化 RabbitMQ 客户端连接
     * @return 成功返回 true，失败返回 false
     */
    bool init_mq_client();

    /**
     * @brief 初始化基于 etcd 的 RPC 信道池（用于调用用户服务）
     * @return 成功返回 true，失败返回 false
     */
    bool init_channel_pool();

    /**
     * @brief 初始化 brpc 服务器，绑定所有已注册的 service
     * @return 成功返回 true，失败返回 false
     */
    bool init_brpc_server();

    /**
     * @brief 初始化 etcd 服务注册客户端，设置租约 TTL
     * @return 成功返回 true，失败返回 false
     */
    bool init_etcd_client();

    // ==================== 成员变量 ====================

    TransmitServerConfig config_;                                       ///< 服务器配置

    int etcd_lease_ttl_;                                                ///< etcd Lease 租约时长
    mylog::LogLevel log_level_;                                         ///< 日志级别

    std::shared_ptr<transmit_table::TransmitTable> transmit_table_;     ///< 数据库操作实例
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;            ///< RPC 信道池
    std::shared_ptr<mq::MQClient> mq_client_;                          ///< MQ 消息队列客户端

    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;          ///< etcd 服务注册客户端
    std::unique_ptr<brpc::Server> brpc_server_;                         ///< brpc 服务器
    std::atomic<bool> running_;                                         ///< 运行状态标志

    std::vector<google::protobuf::Service*> registered_services_;       ///< 已注册的 brpc 服务
};

} // namespace transmit_service

#endif // TRANSMIT_SERVER_BUILDER_HPP
