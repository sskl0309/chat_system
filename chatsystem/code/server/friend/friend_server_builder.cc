// =============================================================================
// friend_server_builder.cc - 好友管理服务器建造者类实现
// =============================================================================
// 实现 FriendServerBuilder 类的所有方法，包括：
//   - 链式配置方法（with_xxx 系列）
//   - 各组件初始化方法（init_xxx 系列）
//   - 构建方法（build）
//   - 运行时方法（start/stop/register_service_to_etcd）
//
// 组件初始化顺序：日志 -> 数据库 -> ES -> 通道池 -> brpc服务器 -> etcd客户端
// 该顺序存在依赖关系：日志最先初始化以便输出后续组件的日志；
// brpc服务器依赖已注册的Service列表；etcd注册依赖服务器启动。
// =============================================================================

#include "friend_server_builder.hpp"

#include <odb/mysql/database.hxx>

namespace friend_service {

// ==================== 构造/析构 ====================

/**
 * @brief 构造函数，初始化默认配置
 *
 * 所有配置项均设置为合理的默认值，可通过 with_xxx 方法覆盖。
 */
FriendServerBuilder::FriendServerBuilder()
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    // 网络配置默认值
    config_.listen_addr = "0.0.0.0";
    config_.listen_port = 10005;
    config_.external_addr = "";
    config_.external_port = 0;

    // etcd 配置默认值
    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;

    // MySQL 配置默认值
    config_.mysql_user = "root";
    config_.mysql_password = "123456";
    config_.mysql_database = "chat_friend";
    config_.mysql_host = "127.0.0.1";
    config_.mysql_port = 0;

    // ES 配置默认值
    config_.es_host = "127.0.0.1";
    config_.es_port = 9200;

    // 日志配置默认值
    config_.is_debug = false;
    config_.log_file = "friend_server.log";
}

/**
 * @brief 析构函数，自动停止服务器并释放资源
 */
FriendServerBuilder::~FriendServerBuilder() {
    stop();
}

// ==================== 链式配置方法实现 ====================
// 每个方法设置对应的配置项并返回 *this 以支持链式调用

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

/**
 * @brief 注册 brpc RPC 服务
 *
 * 将 RPC 服务添加到待注册列表，在 init_brpc_server() 时统一注册到 brpc 服务器。
 *
 * @param service protobuf Service 指针（调用方负责生命周期管理）
 * @return builder 引用，支持链式调用
 */
FriendServerBuilder& FriendServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[FriendServerBuilder] BRPC service registered");
    return *this;
}

// ==================== 私有初始化方法 ====================

/**
 * @brief 初始化日志系统
 * @return true 表示初始化成功
 */
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

/**
 * @brief 初始化 MySQL 数据库连接
 *
 * 使用 ODB MySQL 驱动创建数据库连接，并构造 FriendTable 数据库操作对象。
 *
 * @return true 表示连接成功
 */
bool FriendServerBuilder::init_database() {
    try {
        // 创建 ODB MySQL 数据库连接
        std::shared_ptr<odb::database> db(
            new odb::mysql::database(
                config_.mysql_user,
                config_.mysql_password,
                config_.mysql_database,
                config_.mysql_host,
                static_cast<unsigned int>(config_.mysql_port),
                nullptr,
                "utf8"  // 字符集设为 utf8 支持中文
            )
        );
        // 构造数据库操作对象
        friend_table_ = std::make_shared<friend_table::FriendTable>(db);
        LOG_INFO("[FriendServerBuilder] Database connected: {}@{}:{}/{}",
                 config_.mysql_user, config_.mysql_host, config_.mysql_port, config_.mysql_database);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FriendServerBuilder] Database init failed: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 Elasticsearch 客户端
 * @return true 表示初始化成功
 */
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

/**
 * @brief 初始化 RPC 通道池
 *
 * 基于 etcd 进行服务发现，创建通道池用于调用其他微服务
 * （用户服务、文件服务、消息存储服务等）。
 *
 * @return true 表示初始化成功
 */
bool FriendServerBuilder::init_channel_pool() {
    try {
        channel_pool_ = std::make_shared<brpc::ServiceChannelPool>();
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        // 通过 etcd 进行服务发现，监听 /services 目录下的服务注册信息
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

/**
 * @brief 初始化 brpc 服务器并注册所有 RPC 服务
 *
 * 创建 brpc::Server 实例，将之前通过 register_brpc_service() 注册的
 * 所有 Service 添加到服务器中。
 *
 * @return true 表示初始化成功
 */
bool FriendServerBuilder::init_brpc_server() {
    try {
        brpc_server_ = std::make_unique<brpc::Server>();
        // 将所有已注册的服务添加到 brpc 服务器
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

/**
 * @brief 初始化 etcd 服务注册客户端
 *
 * 创建 etcd 客户端实例，用于后续将好友服务注册到 etcd 实现服务发现。
 *
 * @return true 表示初始化成功
 */
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

/**
 * @brief 构建并初始化所有组件
 *
 * 按依赖顺序依次初始化各组件，任何一步失败都会立即返回 false。
 *
 * @return true 表示全部初始化成功
 */
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

/**
 * @brief 向 etcd 注册当前服务
 *
 * 将好友服务的地址注册到 etcd，使其他微服务能够通过服务发现找到本服务。
 * 优先使用外部地址（external_addr），未设置时使用监听地址。
 *
 * @param service_name 服务名称（如 "friend_service"）
 * @return true 表示注册成功
 */
bool FriendServerBuilder::register_service_to_etcd(const std::string& service_name) {
    if (!etcd_client_) {
        LOG_ERROR("[FriendServerBuilder] Etcd client not initialized");
        return false;
    }
    // 确定注册地址：优先使用外部地址
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

/**
 * @brief 启动 brpc 服务器
 *
 * 在配置的监听地址和端口上启动 brpc 服务器，
 * 设置 idle_timeout 为 -1 表示不主动断开空闲连接。
 *
 * @return true 表示启动成功
 */
bool FriendServerBuilder::start() {
    if (!brpc_server_) {
        LOG_ERROR("[FriendServerBuilder] BRPC server not initialized");
        return false;
    }
    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;  // 不主动断开空闲连接
    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[FriendServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }
    running_ = true;
    LOG_INFO("[FriendServerBuilder] BRPC server started on {}", addr);
    return true;
}

/**
 * @brief 停止服务器，释放各组件资源
 *
 * 按顺序停止：etcd 客户端 -> 通道池 -> brpc 服务器
 */
void FriendServerBuilder::stop() {
    running_ = false;
    // 先停止 etcd 客户端（取消服务注册）
    if (etcd_client_) {
        etcd_client_->stop();
    }
    // 再停止 RPC 通道池
    if (channel_pool_) {
        channel_pool_->stop();
    }
    // 最后停止 brpc 服务器
    if (brpc_server_) {
        brpc_server_->Stop(0);
        brpc_server_->Join();
    }
}

} // namespace friend_service
