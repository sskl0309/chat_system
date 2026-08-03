// =============================================================================
// friend_server_builder.hpp - 好友管理服务器建造者类声明
// =============================================================================
// 采用建造者模式统一管理好友子服务的各组件构造过程。
//
// 功能职责：
//   1. 初始化日志系统（mylog）
//   2. 初始化 MySQL 数据库连接（ODB ORM）
//   3. 初始化 Elasticsearch 客户端
//   4. 初始化 RPC 通道池（用于服务发现与跨服务调用）
//   5. 初始化 brpc 服务器（注册 RPC 服务）
//   6. 初始化 etcd 客户端（服务注册与发现）
//
// 使用方式：
//   FriendServerBuilder builder;
//   builder.with_listen_port(10005)
//          .with_mysql_host("127.0.0.1")
//          .register_brpc_service(&service_impl)
//          .build();
//   builder.start();
//   builder.register_service_to_etcd("friend_service");
// =============================================================================

#ifndef FRIEND_SERVER_BUILDER_HPP
#define FRIEND_SERVER_BUILDER_HPP

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
#include "friend_table.hpp"
#include "friend_es.hpp"

namespace friend_service {

/**
 * @brief 好友管理服务器配置结构体
 *
 * 集中存储服务器运行所需的全部配置参数，包括网络地址、数据库、ES、etcd 等。
 */
struct FriendServerConfig {
    std::string listen_addr;     ///< 服务器监听地址
    int listen_port;             ///< 服务器监听端口
    std::string external_addr;   ///< 外部访问地址（用于 etcd 注册）
    int external_port;           ///< 外部访问端口

    std::string etcd_addr;       ///< etcd 服务器地址
    int etcd_port;               ///< etcd 服务器端口

    std::string mysql_user;      ///< MySQL 用户名
    std::string mysql_password;  ///< MySQL 密码
    std::string mysql_database;  ///< MySQL 数据库名
    std::string mysql_host;      ///< MySQL 服务器地址
    int mysql_port;              ///< MySQL 服务器端口

    std::string es_host;         ///< Elasticsearch 地址
    int es_port;                 ///< Elasticsearch 端口

    bool is_debug;               ///< 是否为调试模式
    std::string log_file;        ///< 日志文件路径
};

/**
 * @brief 好友管理服务器建造者类
 *
 * 使用建造者模式通过链式调用配置并初始化好友管理服务的各组件。
 * 构建顺序：日志 -> 数据库 -> ES -> 通道池 -> brpc服务器 -> etcd客户端
 */
class FriendServerBuilder {
public:
    FriendServerBuilder();
    ~FriendServerBuilder();

    // ==================== 链式配置方法 ====================

    /// 设置服务器监听地址
    FriendServerBuilder& with_listen_address(const std::string& addr);
    /// 设置服务器监听端口
    FriendServerBuilder& with_listen_port(int port);
    /// 设置外部访问地址（etcd 注册用）
    FriendServerBuilder& with_external_address(const std::string& addr);
    /// 设置外部访问端口（etcd 注册用）
    FriendServerBuilder& with_external_port(int port);

    /// 设置 etcd 服务器地址
    FriendServerBuilder& with_etcd_address(const std::string& addr);
    /// 设置 etcd 服务器端口
    FriendServerBuilder& with_etcd_port(int port);
    /// 设置 etcd 租约 TTL（秒）
    FriendServerBuilder& with_etcd_lease_ttl(int ttl);

    /// 设置 MySQL 用户名
    FriendServerBuilder& with_mysql_user(const std::string& user);
    /// 设置 MySQL 密码
    FriendServerBuilder& with_mysql_password(const std::string& password);
    /// 设置 MySQL 数据库名
    FriendServerBuilder& with_mysql_database(const std::string& database);
    /// 设置 MySQL 服务器地址
    FriendServerBuilder& with_mysql_host(const std::string& host);
    /// 设置 MySQL 服务器端口
    FriendServerBuilder& with_mysql_port(int port);

    /// 设置 Elasticsearch 地址
    FriendServerBuilder& with_es_host(const std::string& host);
    /// 设置 Elasticsearch 端口
    FriendServerBuilder& with_es_port(int port);

    /// 设置是否为调试模式
    FriendServerBuilder& with_debug_mode(bool debug);
    /// 设置日志文件路径
    FriendServerBuilder& with_log_file(const std::string& file);
    /// 设置日志级别
    FriendServerBuilder& with_log_level(mylog::LogLevel level);

    /// 注册 brpc RPC 服务（可多次调用注册多个服务）
    FriendServerBuilder& register_brpc_service(google::protobuf::Service* service);

    // ==================== 构建与运行 ====================

    /**
     * @brief 构建并初始化所有组件
     *
     * 按顺序初始化：日志 -> 数据库 -> ES -> 通道池 -> brpc服务器 -> etcd客户端。
     * 任何一步失败都会返回 false。
     *
     * @return true 表示全部初始化成功
     */
    bool build();

    // ==================== 组件访问器 ====================

    /// 获取数据库操作对象
    std::shared_ptr<friend_table::FriendTable> get_friend_table() const { return friend_table_; }
    /// 获取 ES 搜索客户端
    std::shared_ptr<friend_es::FriendES> get_friend_es() const { return friend_es_; }
    /// 获取 RPC 通道池
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const { return channel_pool_; }
    /// 获取 etcd 服务注册客户端
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client() { return etcd_client_; }
    /// 获取 brpc 服务器实例
    brpc::Server* get_brpc_server() { return brpc_server_.get(); }

    /**
     * @brief 向 etcd 注册当前服务
     * @param service_name 服务名称（如 "friend_service"）
     * @return true 表示注册成功
     */
    bool register_service_to_etcd(const std::string& service_name);

    /**
     * @brief 启动 brpc 服务器
     * @return true 表示启动成功
     */
    bool start();

    /// 停止服务器，释放各组件资源
    void stop();

private:
    // ==================== 私有初始化方法 ====================

    bool init_logger();       ///< 初始化日志系统
    bool init_database();     ///< 初始化 MySQL 数据库连接
    bool init_es();           ///< 初始化 Elasticsearch 客户端
    bool init_channel_pool(); ///< 初始化 RPC 通道池（基于 etcd 服务发现）
    bool init_brpc_server();  ///< 初始化 brpc 服务器并注册服务
    bool init_etcd_client();  ///< 初始化 etcd 服务注册客户端

    // ==================== 成员变量 ====================

    FriendServerConfig config_;                    ///< 服务器配置
    int etcd_lease_ttl_;                           ///< etcd 租约 TTL（秒）
    mylog::LogLevel log_level_;                    ///< 日志级别

    std::shared_ptr<friend_table::FriendTable> friend_table_;   ///< 数据库操作对象
    std::shared_ptr<friend_es::FriendES> friend_es_;            ///< ES 搜索客户端
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;    ///< RPC 通道池

    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;  ///< etcd 服务注册客户端
    std::unique_ptr<brpc::Server> brpc_server_;                 ///< brpc 服务器实例
    std::atomic<bool> running_;                                 ///< 服务器运行状态标志

    std::vector<google::protobuf::Service*> registered_services_;  ///< 已注册的 RPC 服务列表
};

} // namespace friend_service

#endif
