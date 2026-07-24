// =============================================================================
// user_server_builder.hpp - 用户服务器建造者类声明（建造者模式）
// =============================================================================
// 本头文件声明 UserServerBuilder 类，采用建造者模式统一管理用户子服务的
// 各组件构造过程，包括日志、数据库、Redis、邮件、ES、brpc 服务、etcd 客户端、
// 以及文件服务 RPC 信道池。
//
// 设计模式：建造者模式（Builder Pattern）
//   - 通过链式调用（Fluent API）配置服务器参数，代码清晰简洁
//   - 将复杂对象的构造过程与表示分离，便于扩展配置项
// =============================================================================

#ifndef USER_SERVER_BUILDER_HPP
#define USER_SERVER_BUILDER_HPP

#include <string>
#include <memory>
#include <vector>
#include <atomic>

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <odb/database.hxx>

#include "log.hpp"
#include "etcd_client.hpp"
#include "brpc_client.hpp"
#include "user_table.hpp"
#include "redis_client.hpp"
#include "email_client.hpp"
#include "user_es.hpp"

namespace user_service {

/**
 * @brief 用户服务器配置结构体
 */
struct UserServerConfig {
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

    // Redis 配置
    std::string redis_host;         ///< Redis 主机地址
    int redis_port;                 ///< Redis 端口

    // ES 配置
    std::string es_host;            ///< ES 主机地址
    int es_port;                    ///< ES 端口

    // 邮件配置
    std::string smtp_server;        ///< SMTP 服务器地址
    int smtp_port;                  ///< SMTP 服务器端口
    std::string sender_email;       ///< 发件人邮箱
    std::string email_auth_code;    ///< SMTP 授权码

    // 日志配置
    bool is_debug;                  ///< 是否调试模式
    std::string log_file;           ///< 日志文件路径
};

/**
 * @brief 用户服务器建造者类
 *
 * 采用建造者模式，统一管理 brpc 服务、数据库、Redis、邮件、ES、etcd 客户端、
 * 文件服务 RPC 信道池的构造过程。
 */
class UserServerBuilder {
public:
    UserServerBuilder();
    ~UserServerBuilder();

    // ==================== 配置方法（链式调用） ====================
    // 每个 with_xxx 方法设置一项配置并返回 *this，支持链式调用
    // 示例：builder.with_listen_port(8080).with_mysql_user("root").build();

    /// 设置服务监听地址
    UserServerBuilder& with_listen_address(const std::string& addr);
    /// 设置服务监听端口
    UserServerBuilder& with_listen_port(int port);
    /// 设置对外暴露地址（用于 etcd 服务注册，通常为公网IP）
    UserServerBuilder& with_external_address(const std::string& addr);
    /// 设置对外暴露端口
    UserServerBuilder& with_external_port(int port);

    /// 设置 etcd 服务器地址
    UserServerBuilder& with_etcd_address(const std::string& addr);
    /// 设置 etcd 服务器端口
    UserServerBuilder& with_etcd_port(int port);
    /// 设置 etcd 租约 TTL（秒），到期后自动注销服务
    UserServerBuilder& with_etcd_lease_ttl(int ttl);

    /// 设置 MySQL 用户名
    UserServerBuilder& with_mysql_user(const std::string& user);
    /// 设置 MySQL 密码
    UserServerBuilder& with_mysql_password(const std::string& password);
    /// 设置 MySQL 数据库名
    UserServerBuilder& with_mysql_database(const std::string& database);
    /// 设置 MySQL 主机地址
    UserServerBuilder& with_mysql_host(const std::string& host);
    /// 设置 MySQL 端口（0 表示使用默认端口 3306）
    UserServerBuilder& with_mysql_port(int port);

    /// 设置 Redis 主机地址
    UserServerBuilder& with_redis_host(const std::string& host);
    /// 设置 Redis 端口
    UserServerBuilder& with_redis_port(int port);

    /// 设置 Elasticsearch 主机地址
    UserServerBuilder& with_es_host(const std::string& host);
    /// 设置 Elasticsearch 端口
    UserServerBuilder& with_es_port(int port);

    /// 设置 SMTP 服务器地址（用于发送验证码邮件）
    UserServerBuilder& with_smtp_server(const std::string& server);
    /// 设置 SMTP 服务器端口
    UserServerBuilder& with_smtp_port(int port);
    /// 设置发件人邮箱地址
    UserServerBuilder& with_sender_email(const std::string& email);
    /// 设置 SMTP 授权码（非邮箱登录密码）
    UserServerBuilder& with_email_auth_code(const std::string& code);

    /// 设置调试模式（控制台彩色输出 vs 文件输出）
    UserServerBuilder& with_debug_mode(bool debug);
    /// 设置日志文件路径
    UserServerBuilder& with_log_file(const std::string& file);
    /// 设置日志级别
    UserServerBuilder& with_log_level(mylog::LogLevel level);

    // ==================== 服务注册与构建 ====================

    /**
     * @brief 注册 brpc 服务实现
     *
     * 允许多次调用注册多个 service，最终一起绑定到 brpc::Server。
     * 注意：传入的 service 指针生命周期需长于 brpc::Server。
     */
    UserServerBuilder& register_brpc_service(google::protobuf::Service* service);

    /**
     * @brief 构建所有组件
     *
     * 按顺序初始化：日志、数据库、Redis、邮件、ES、文件服务信道池、brpc服务器、etcd客户端
     */
    bool build();

    // ==================== 访问器方法 ====================

    std::shared_ptr<user_table::UserTable> get_user_table() const;
    std::shared_ptr<redis_client::RedisClient> get_redis_client() const;
    std::shared_ptr<email_client::EmailClient> get_email_client() const;
    std::shared_ptr<user_es::UserES> get_user_es() const;
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const;
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client();
    brpc::Server* get_brpc_server();

    // ==================== 运行时方法 ====================

    /**
     * @brief 向 etcd 注册当前服务
     *
     * 向 etcd 写入 /services/{service_name}/{host_address}，
     * 并启动租约保活线程，TTL 到期后自动移除。
     *
     * @param service_name 服务名称（如 "user_service"）
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
     * 按顺序停止：etcd 保活 → 信道池 → brpc 服务器（优雅退出 + Join 等待）
     */
    void stop();

private:
    // ==================== 组件初始化方法 ====================
    // 每个 init_xxx 负责初始化一个子系统，失败返回 false 将中断整个 build() 流程

    /// 初始化日志系统
    bool init_logger();
    /// 初始化 MySQL 数据库连接，创建 UserTable 实例
    bool init_database();
    /// 初始化 Redis 客户端连接
    bool init_redis();
    /// 初始化邮件发送客户端
    bool init_email_client();
    /// 初始化 ES 客户端，并尝试创建用户索引
    bool init_es();
    /// 初始化基于 etcd 的 RPC 信道池（用于调用文件服务等）
    bool init_channel_pool();
    /// 初始化 brpc 服务器，绑定所有已注册的 service
    bool init_brpc_server();
    /// 初始化 etcd 服务注册客户端，设置租约 TTL
    bool init_etcd_client();

    UserServerConfig config_;                                       ///< 服务器配置
    int etcd_lease_ttl_;                                            ///< etcd Lease 租约时长
    mylog::LogLevel log_level_;                                     ///< 日志级别

    std::shared_ptr<user_table::UserTable> user_table_;             ///< 用户数据库操作
    std::shared_ptr<redis_client::RedisClient> redis_client_;       ///< Redis 客户端
    std::shared_ptr<email_client::EmailClient> email_client_;       ///< 邮件客户端
    std::shared_ptr<user_es::UserES> user_es_;                      ///< ES 用户数据管理
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;        ///< 文件服务 RPC 信道池
    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;      ///< etcd 服务注册客户端
    std::unique_ptr<brpc::Server> brpc_server_;                     ///< brpc 服务器
    std::atomic<bool> running_;                                     ///< 运行状态标志

    std::vector<google::protobuf::Service*> registered_services_;   ///< 已注册的 brpc 服务
};

} // namespace user_service

#endif // USER_SERVER_BUILDER_HPP
