// =============================================================================
// friend_server_builder.hpp - 好友管理服务器建造者类声明
// =============================================================================
// 采用建造者模式统一管理好友子服务的各组件构造过程。
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

struct FriendServerConfig {
    std::string listen_addr;
    int listen_port;
    std::string external_addr;
    int external_port;

    std::string etcd_addr;
    int etcd_port;

    std::string mysql_user;
    std::string mysql_password;
    std::string mysql_database;
    std::string mysql_host;
    int mysql_port;

    std::string es_host;
    int es_port;

    bool is_debug;
    std::string log_file;
};

class FriendServerBuilder {
public:
    FriendServerBuilder();
    ~FriendServerBuilder();

    FriendServerBuilder& with_listen_address(const std::string& addr);
    FriendServerBuilder& with_listen_port(int port);
    FriendServerBuilder& with_external_address(const std::string& addr);
    FriendServerBuilder& with_external_port(int port);

    FriendServerBuilder& with_etcd_address(const std::string& addr);
    FriendServerBuilder& with_etcd_port(int port);
    FriendServerBuilder& with_etcd_lease_ttl(int ttl);

    FriendServerBuilder& with_mysql_user(const std::string& user);
    FriendServerBuilder& with_mysql_password(const std::string& password);
    FriendServerBuilder& with_mysql_database(const std::string& database);
    FriendServerBuilder& with_mysql_host(const std::string& host);
    FriendServerBuilder& with_mysql_port(int port);

    FriendServerBuilder& with_es_host(const std::string& host);
    FriendServerBuilder& with_es_port(int port);

    FriendServerBuilder& with_debug_mode(bool debug);
    FriendServerBuilder& with_log_file(const std::string& file);
    FriendServerBuilder& with_log_level(mylog::LogLevel level);

    FriendServerBuilder& register_brpc_service(google::protobuf::Service* service);

    bool build();

    std::shared_ptr<friend_table::FriendTable> get_friend_table() const { return friend_table_; }
    std::shared_ptr<friend_es::FriendES> get_friend_es() const { return friend_es_; }
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const { return channel_pool_; }
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client() { return etcd_client_; }
    brpc::Server* get_brpc_server() { return brpc_server_.get(); }

    bool register_service_to_etcd(const std::string& service_name);
    bool start();
    void stop();

private:
    bool init_logger();
    bool init_database();
    bool init_es();
    bool init_channel_pool();
    bool init_brpc_server();
    bool init_etcd_client();

    FriendServerConfig config_;
    int etcd_lease_ttl_;
    mylog::LogLevel log_level_;

    std::shared_ptr<friend_table::FriendTable> friend_table_;
    std::shared_ptr<friend_es::FriendES> friend_es_;
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;

    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;
    std::unique_ptr<brpc::Server> brpc_server_;
    std::atomic<bool> running_;

    std::vector<google::protobuf::Service*> registered_services_;
};

} // namespace friend_service

#endif
