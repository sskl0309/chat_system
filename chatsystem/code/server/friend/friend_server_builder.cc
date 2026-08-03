// =============================================================================
// friend_server_builder.cc - 好友管理服务器建造者类实现
// =============================================================================

#include "friend_server_builder.hpp"

#include <odb/mysql/database.hxx>

namespace friend_service {

FriendServerBuilder::FriendServerBuilder()
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    config_.listen_addr = "0.0.0.0";
    config_.listen_port = 10005;
    config_.external_addr = "";
    config_.external_port = 0;

    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;

    config_.mysql_user = "root";
    config_.mysql_password = "123456";
    config_.mysql_database = "chat_friend";
    config_.mysql_host = "127.0.0.1";
    config_.mysql_port = 0;

    config_.es_host = "127.0.0.1";
    config_.es_port = 9200;

    config_.is_debug = false;
    config_.log_file = "friend_server.log";
}

FriendServerBuilder::~FriendServerBuilder() {
    stop();
}

FriendServerBuilder& FriendServerBuilder::with_listen_address(const std::string& addr) {
    config_.listen_addr = addr; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_listen_port(int port) {
    config_.listen_port = port; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_external_address(const std::string& addr) {
    config_.external_addr = addr; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_external_port(int port) {
    config_.external_port = port; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_etcd_lease_ttl(int ttl) {
    etcd_lease_ttl_ = ttl; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_mysql_user(const std::string& user) {
    config_.mysql_user = user; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_mysql_password(const std::string& password) {
    config_.mysql_password = password; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_mysql_database(const std::string& database) {
    config_.mysql_database = database; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_mysql_host(const std::string& host) {
    config_.mysql_host = host; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_mysql_port(int port) {
    config_.mysql_port = port; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_es_host(const std::string& host) {
    config_.es_host = host; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_es_port(int port) {
    config_.es_port = port; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file; return *this;
}
FriendServerBuilder& FriendServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level; return *this;
}

FriendServerBuilder& FriendServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[FriendServerBuilder] BRPC service registered");
    return *this;
}

// ==================== 私有初始化 ====================

bool FriendServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[FriendServerBuilder] Logger initialized");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FriendServerBuilder] Logger init failed: " << e.what() << std::endl;
        return false;
    }
}

bool FriendServerBuilder::init_database() {
    try {
        std::shared_ptr<odb::database> db(
            new odb::mysql::database(
                config_.mysql_user,
                config_.mysql_password,
                config_.mysql_database,
                config_.mysql_host,
                static_cast<unsigned int>(config_.mysql_port),
                nullptr,
                "utf8"
            )
        );
        friend_table_ = std::make_shared<friend_table::FriendTable>(db);
        LOG_INFO("[FriendServerBuilder] Database connected: {}@{}:{}/{}",
                 config_.mysql_user, config_.mysql_host, config_.mysql_port, config_.mysql_database);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FriendServerBuilder] Database init failed: {}", e.what());
        return false;
    }
}

bool FriendServerBuilder::init_es() {
    try {
        friend_es_ = std::make_shared<friend_es::FriendES>(
            config_.es_host, config_.es_port);
        LOG_INFO("[FriendServerBuilder] ES initialized: {}:{}", config_.es_host, config_.es_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FriendServerBuilder] ES init failed: {}", e.what());
        return false;
    }
}

bool FriendServerBuilder::init_channel_pool() {
    try {
        channel_pool_ = std::make_shared<brpc::ServiceChannelPool>();
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        if (!channel_pool_->init_with_etcd(etcd_addr, "/services")) {
            LOG_ERROR("[FriendServerBuilder] Channel pool init failed");
            return false;
        }
        LOG_INFO("[FriendServerBuilder] Channel pool initialized with etcd");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FriendServerBuilder] Channel pool init failed: {}", e.what());
        return false;
    }
}

bool FriendServerBuilder::init_brpc_server() {
    try {
        brpc_server_ = std::make_unique<brpc::Server>();
        for (auto service : registered_services_) {
            if (brpc_server_->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
                LOG_ERROR("[FriendServerBuilder] Failed to add service");
                return false;
            }
        }
        LOG_INFO("[FriendServerBuilder] BRPC server initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FriendServerBuilder] BRPC server init failed: {}", e.what());
        return false;
    }
}

bool FriendServerBuilder::init_etcd_client() {
    try {
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        etcd_client_ = std::make_unique<etcd::ServiceRegisterClient>(etcd_addr, etcd_lease_ttl_);
        LOG_INFO("[FriendServerBuilder] Etcd client initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FriendServerBuilder] Etcd client init failed: {}", e.what());
        return false;
    }
}

bool FriendServerBuilder::build() {
    if (!init_logger()) return false;
    if (!init_database()) return false;
    if (!init_es()) return false;
    if (!init_channel_pool()) return false;
    if (!init_brpc_server()) return false;
    if (!init_etcd_client()) return false;

    LOG_INFO("[FriendServerBuilder] All components built");
    return true;
}

// ==================== 运行时方法 ====================

bool FriendServerBuilder::register_service_to_etcd(const std::string& service_name) {
    if (!etcd_client_) {
        LOG_ERROR("[FriendServerBuilder] Etcd client not initialized");
        return false;
    }
    std::string host_address;
    if (!config_.external_addr.empty() && config_.external_port > 0) {
        host_address = config_.external_addr + ":" + std::to_string(config_.external_port);
    } else {
        host_address = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    }
    bool result = etcd_client_->register_service(service_name, host_address);
    if (result) {
        LOG_INFO("[FriendServerBuilder] Registered to etcd: {} -> {}", service_name, host_address);
    } else {
        LOG_ERROR("[FriendServerBuilder] Failed to register service");
    }
    return result;
}

bool FriendServerBuilder::start() {
    if (!brpc_server_) {
        LOG_ERROR("[FriendServerBuilder] BRPC server not initialized");
        return false;
    }
    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[FriendServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }
    running_ = true;
    LOG_INFO("[FriendServerBuilder] BRPC server started on {}", addr);
    return true;
}

void FriendServerBuilder::stop() {
    running_ = false;
    if (etcd_client_) {
        etcd_client_->stop();
    }
    if (channel_pool_) {
        channel_pool_->stop();
    }
    if (brpc_server_) {
        brpc_server_->Stop(0);
        brpc_server_->Join();
    }
}

} // namespace friend_service
