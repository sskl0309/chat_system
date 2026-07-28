// =============================================================================
// message_server_builder.hpp - 消息存储服务器建造者类声明（建造者模式）
// =============================================================================
// 本头文件声明 MessageServerBuilder 类，采用建造者模式统一管理消息存储子服务的
// 各组件构造过程，包括日志、数据库、ES、etcd 客户端、brpc 服务、RPC 信道池、MQ 客户端。
//
// 设计模式：建造者模式（Builder Pattern）
//   - 通过链式调用（Fluent API）配置服务器参数
//   - 将复杂对象的构造过程与表示分离
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
 */
struct MessageServerConfig {
    // 网络配置
    std::string listen_addr;
    int listen_port;
    std::string external_addr;
    int external_port;

    // etcd 配置
    std::string etcd_addr;
    int etcd_port;

    // MySQL 数据库配置
    std::string mysql_user;
    std::string mysql_password;
    std::string mysql_database;
    std::string mysql_host;
    int mysql_port;

    // ES 配置
    std::string es_host;
    int es_port;

    // RabbitMQ 配置
    std::string mq_host;
    int mq_port;
    std::string mq_user;
    std::string mq_password;
    std::string mq_vhost;

    // 日志配置
    bool is_debug;
    std::string log_file;
};

/**
 * @brief 消息存储服务器建造者类
 */
class MessageServerBuilder {
public:
    MessageServerBuilder();
    ~MessageServerBuilder();

    // ==================== 配置方法（链式调用） ====================

    MessageServerBuilder& with_listen_address(const std::string& addr);
    MessageServerBuilder& with_listen_port(int port);
    MessageServerBuilder& with_external_address(const std::string& addr);
    MessageServerBuilder& with_external_port(int port);

    MessageServerBuilder& with_etcd_address(const std::string& addr);
    MessageServerBuilder& with_etcd_port(int port);
    MessageServerBuilder& with_etcd_lease_ttl(int ttl);

    MessageServerBuilder& with_mysql_user(const std::string& user);
    MessageServerBuilder& with_mysql_password(const std::string& password);
    MessageServerBuilder& with_mysql_database(const std::string& database);
    MessageServerBuilder& with_mysql_host(const std::string& host);
    MessageServerBuilder& with_mysql_port(int port);

    MessageServerBuilder& with_es_host(const std::string& host);
    MessageServerBuilder& with_es_port(int port);

    MessageServerBuilder& with_mq_host(const std::string& host);
    MessageServerBuilder& with_mq_port(int port);
    MessageServerBuilder& with_mq_user(const std::string& user);
    MessageServerBuilder& with_mq_password(const std::string& password);
    MessageServerBuilder& with_mq_vhost(const std::string& vhost);

    MessageServerBuilder& with_debug_mode(bool debug);
    MessageServerBuilder& with_log_file(const std::string& file);
    MessageServerBuilder& with_log_level(mylog::LogLevel level);

    // ==================== 服务注册与构建 ====================

    MessageServerBuilder& register_brpc_service(google::protobuf::Service* service);

    bool build();

    // ==================== 访问器方法 ====================

    std::shared_ptr<message_table::MessageTable> get_message_table() const;
    std::shared_ptr<message_es::MessageES> get_message_es() const;
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const;
    std::shared_ptr<mq::MQClient> get_mq_client() const;
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client();
    brpc::Server* get_brpc_server();

    // ==================== 运行时方法 ====================

    bool register_service_to_etcd(const std::string& service_name);
    bool start();
    void stop();

private:
    bool init_logger();
    bool init_database();
    bool init_es();
    bool init_mq_client();
    bool init_channel_pool();
    bool init_brpc_server();
    bool init_etcd_client();

    MessageServerConfig config_;
    int etcd_lease_ttl_;
    mylog::LogLevel log_level_;

    std::shared_ptr<message_table::MessageTable> message_table_;
    std::shared_ptr<message_es::MessageES> message_es_;
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;
    std::shared_ptr<mq::MQClient> mq_client_;

    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;
    std::unique_ptr<brpc::Server> brpc_server_;
    std::atomic<bool> running_;

    std::vector<google::protobuf::Service*> registered_services_;
};

} // namespace message_service

#endif // MESSAGE_SERVER_BUILDER_HPP