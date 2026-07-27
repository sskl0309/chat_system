// =============================================================================
// transmit_server_builder.cc - 消息转发服务器建造者类实现
// =============================================================================
// 本文件实现 TransmitServerBuilder 类的所有方法，包括配置方法、组件初始化方法、
// 构建方法、访问器方法和运行时方法。
//
// 初始化顺序（严格依赖顺序）：
//   1. 日志系统（最优先，后续组件需要日志输出）
//   2. MySQL 数据库（提供会话成员查询）
//   3. RabbitMQ 客户端（消息发布）
//   4. RPC 信道池（调用用户服务）
//   5. brpc 服务器（注册 RPC 服务）
//   6. etcd 服务注册客户端（最后，服务器就绪后才注册）
//
// 关闭顺序（启动的逆序）：
//   1. etcd 租约保活（停止服务注册）
//   2. MQ 客户端（停止消息发布）
//   3. RPC 信道池（释放下游连接）
//   4. brpc 服务器（优雅退出）
// =============================================================================

#include "transmit_server_builder.hpp"

#include <odb/mysql/database.hxx>

namespace transmit_service {

// =============================================================================
// 构造与析构函数
// =============================================================================

/**
 * @brief 默认构造函数
 * 
 * 初始化默认配置参数，包括网络、etcd、MySQL、RabbitMQ、日志等配置。
 */
TransmitServerBuilder::TransmitServerBuilder()
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    // ---------- 网络配置 ----------
    config_.listen_addr = "0.0.0.0";       // 监听所有网卡
    config_.listen_port = 10003;           // 默认监听端口（消息转发服务）
    config_.external_addr = "";            // 留空则用 listen_addr 替代
    config_.external_port = 0;             // 留空则用 listen_port 替代

    // ---------- etcd 配置 ----------
    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;              // etcd 默认端口

    // ---------- MySQL 配置 ----------
    config_.mysql_user = "root";
    config_.mysql_password = "123456";
    config_.mysql_database = "chat_friend"; // 会话数据存储在好友数据库中
    config_.mysql_host = "127.0.0.1";
    config_.mysql_port = 0;                // 0 = 使用 MySQL 默认端口 3306

    // ---------- RabbitMQ 配置 ----------
    config_.mq_host = "127.0.0.1";
    config_.mq_port = 5672;                // RabbitMQ 默认端口
    config_.mq_user = "guest";             // RabbitMQ 默认用户名
    config_.mq_password = "guest";         // RabbitMQ 默认密码
    config_.mq_vhost = "/";                // RabbitMQ 默认虚拟主机

    // ---------- 日志配置 ----------
    config_.is_debug = false;
    config_.log_file = "transmit_server.log";
}

/**
 * @brief 析构函数
 * 
 * 自动调用 stop() 停止所有组件，释放资源。
 */
TransmitServerBuilder::~TransmitServerBuilder() {
    stop();
}

// =============================================================================
// 配置方法实现
// =============================================================================

TransmitServerBuilder& TransmitServerBuilder::with_listen_address(const std::string& addr) {
    config_.listen_addr = addr;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_listen_port(int port) {
    config_.listen_port = port;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_external_address(const std::string& addr) {
    config_.external_addr = addr;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_external_port(int port) {
    config_.external_port = port;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_etcd_lease_ttl(int ttl) {
    etcd_lease_ttl_ = ttl;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mysql_user(const std::string& user) {
    config_.mysql_user = user;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mysql_password(const std::string& password) {
    config_.mysql_password = password;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mysql_database(const std::string& database) {
    config_.mysql_database = database;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mysql_host(const std::string& host) {
    config_.mysql_host = host;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mysql_port(int port) {
    config_.mysql_port = port;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mq_host(const std::string& host) {
    config_.mq_host = host;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mq_port(int port) {
    config_.mq_port = port;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mq_user(const std::string& user) {
    config_.mq_user = user;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mq_password(const std::string& password) {
    config_.mq_password = password;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_mq_vhost(const std::string& vhost) {
    config_.mq_vhost = vhost;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file;
    return *this;
}

TransmitServerBuilder& TransmitServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level;
    return *this;
}

// =============================================================================
// 服务注册
// =============================================================================

/**
 * @brief 注册 brpc 服务实现
 * 
 * 将服务实现指针添加到注册列表中，build() 时统一绑定到 brpc 服务器。
 */
TransmitServerBuilder& TransmitServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[TransmitServerBuilder] BRPC service registered");
    return *this;
}

// =============================================================================
// 私有初始化方法
// =============================================================================

/**
 * @brief 初始化日志系统
 * 
 * 日志系统必须最先初始化，因为后续所有组件初始化都可能产生日志输出。
 * 注意：日志初始化失败的日志必须通过 std::cerr 输出（此时日志系统还不可用）。
 */
bool TransmitServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[TransmitServerBuilder] Logger initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[TransmitServerBuilder] Failed to init logger: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 初始化 MySQL 数据库连接
 * 
 * 使用 ODB ORM 框架的 MySQL 后端，创建 odb::database 对象后注入 TransmitTable。
 * 连接池由 ODB 内部管理（线程安全），整个服务生命周期共享同一个 database 实例。
 * 
 * 设计考量：
 *   - port=0 时 ODB 使用 MySQL 默认端口 3306
 *   - socket 传 nullptr 表示使用 TCP 连接（而非 Unix domain socket）
 *   - charset=utf8 确保中文数据正确存储
 */
bool TransmitServerBuilder::init_database() {
    try {
        // 构造 ODB MySQL 数据库连接
        // 参数：user, password, database, host, port, socket(nullptr=TCP), charset
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
        // 将数据库实例注入 TransmitTable，后续所有 CRUD 操作共享此连接
        transmit_table_ = std::make_shared<transmit_table::TransmitTable>(db);
        LOG_INFO("[TransmitServerBuilder] Database initialized: {}@{}:{}/{}",
                 config_.mysql_user, config_.mysql_host, config_.mysql_port, config_.mysql_database);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[TransmitServerBuilder] Failed to init database: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 RabbitMQ 客户端连接
 * 
 * 创建 MQClient 实例并启动，用于将消息发布到消息队列。
 * 消息存储子服务会消费此消息并进行持久化存储。
 */
bool TransmitServerBuilder::init_mq_client() {
    try {
        // 创建 MQ 客户端实例
        mq_client_ = std::make_shared<mq::MQClient>(
            config_.mq_host,
            static_cast<uint16_t>(config_.mq_port),
            config_.mq_user,
            config_.mq_password,
            config_.mq_vhost
        );

        // 启动 MQ 客户端
        if (!mq_client_->start()) {
            LOG_ERROR("[TransmitServerBuilder] Failed to start MQ client");
            return false;
        }

        // 声明消息交换机（direct 类型，持久化）
        // 消息存储服务会绑定到此交换机进行消息消费
        mq_client_->declareExchangeAndBind("message_exchange", AMQP::direct);

        LOG_INFO("[TransmitServerBuilder] MQ client initialized: {}:{}", config_.mq_host, config_.mq_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[TransmitServerBuilder] Failed to init MQ client: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 RPC 信道池
 * 
 * ServiceChannelPool 基于 etcd 自动发现下游服务（目前主要是 user_service）。
 * init_with_etcd 会在 /services 前缀下 watch 服务注册变更，
 * 当有新服务实例上线或下线时，自动更新信道池中的连接。
 * 这样消息转发服务在获取发送者信息时，无需硬编码用户服务地址，支持动态扩缩容。
 */
bool TransmitServerBuilder::init_channel_pool() {
    try {
        // ServiceChannelPool 基于 etcd 自动发现下游服务
        // init_with_etcd 会监听 /services 前缀下的服务注册变更，动态维护信道池
        channel_pool_ = std::make_shared<brpc::ServiceChannelPool>();
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        if (!channel_pool_->init_with_etcd(etcd_addr, "/services")) {
            LOG_ERROR("[TransmitServerBuilder] Failed to init channel pool with etcd");
            return false;
        }
        LOG_INFO("[TransmitServerBuilder] Channel pool initialized with etcd: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[TransmitServerBuilder] Failed to init channel pool: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 brpc 服务器
 * 
 * 创建 brpc::Server 实例并绑定所有已注册的 protobuf service。
 * SERVER_DOESNT_OWN_SERVICE 标志表示 brpc::Server 不负责释放 service 对象，
 * service 的生命周期由调用方（main 函数中的 service_impl 栈对象）管理。
 */
bool TransmitServerBuilder::init_brpc_server() {
    try {
        brpc_server_ = std::make_unique<brpc::Server>();
        for (auto service : registered_services_) {
            if (brpc_server_->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
                LOG_ERROR("[TransmitServerBuilder] Failed to add service");
                return false;
            }
        }
        LOG_INFO("[TransmitServerBuilder] BRPC server initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[TransmitServerBuilder] Failed to init BRPC server: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 etcd 服务注册客户端
 * 
 * etcd 客户端负责将本服务实例注册到 etcd、维持租约保活。
 * 租约 TTL 后如未续约，etcd 会自动删除该 key，实现故障自动摘除。
 * 初始化放在最后，因为信道池（init_channel_pool）也需要访问 etcd。
 */
bool TransmitServerBuilder::init_etcd_client() {
    try {
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        etcd_client_ = std::make_unique<etcd::ServiceRegisterClient>(etcd_addr, etcd_lease_ttl_);
        LOG_INFO("[TransmitServerBuilder] Etcd client initialized: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[TransmitServerBuilder] Failed to init etcd client: {}", e.what());
        return false;
    }
}

// =============================================================================
// 构建所有组件
// =============================================================================

/**
 * @brief 构建所有组件
 * 
 * 按严格的依赖顺序初始化，短路失败：任一步骤失败立即返回 false。
 * 
 * 初始化顺序的依据：
 *   1. 日志：必须在最前面，以便后续组件输出错误日志
 *   2. 数据库：提供会话成员查询功能
 *   3. MQ 客户端：消息发布到队列
 *   4. 信道池：依赖 etcd 用于服务发现（但使用独立连接）
 *   5. brpc：需在所有依赖组件就绪后才能启动
 *   6. etcd 注册客户端：最后初始化，待服务器完全就绪后才注册（避免流量过早路由）
 */
bool TransmitServerBuilder::build() {
    // 按依赖顺序初始化：日志最先，etcd 最后（其他组件可能依赖日志输出）
    if (!init_logger()) return false;           // 1. 日志系统（最优先）
    if (!init_database()) return false;         // 2. MySQL 数据库
    if (!init_mq_client()) return false;        // 3. RabbitMQ 消息队列
    if (!init_channel_pool()) return false;     // 4. RPC 信道池（etcd 服务发现）
    if (!init_brpc_server()) return false;      // 5. brpc 服务器
    if (!init_etcd_client()) return false;      // 6. etcd 注册客户端

    LOG_INFO("[TransmitServerBuilder] All components built successfully");
    return true;
}

// =============================================================================
// 访问器方法
// =============================================================================

std::shared_ptr<transmit_table::TransmitTable> TransmitServerBuilder::get_transmit_table() const {
    return transmit_table_;
}

std::shared_ptr<brpc::ServiceChannelPool> TransmitServerBuilder::get_channel_pool() const {
    return channel_pool_;
}

std::shared_ptr<mq::MQClient> TransmitServerBuilder::get_mq_client() const {
    return mq_client_;
}

std::unique_ptr<etcd::ServiceRegisterClient>& TransmitServerBuilder::get_etcd_client() {
    return etcd_client_;
}

brpc::Server* TransmitServerBuilder::get_brpc_server() {
    return brpc_server_.get();
}

// =============================================================================
// 运行时方法
// =============================================================================

/**
 * @brief 向 etcd 注册当前服务
 * 
 * 向 etcd 写入 /services/{service_name}/{host_address}，
 * 并启动租约保活线程，TTL 到期后自动移除。
 * 
 * 优先使用外部地址（公网IP），其次使用监听地址（内网IP）。
 */
bool TransmitServerBuilder::register_service_to_etcd(const std::string& service_name) {
    if (!etcd_client_) {
        LOG_ERROR("[TransmitServerBuilder] Etcd client not initialized");
        return false;
    }

    // 优先使用外部地址（公网IP），其次使用监听地址（内网IP）
    std::string host_address;
    if (!config_.external_addr.empty() && config_.external_port > 0) {
        host_address = config_.external_addr + ":" + std::to_string(config_.external_port);
    } else {
        host_address = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    }

    // 向 etcd 注册：/services/{service_name}/{host_address}
    // 同时启动租约保活线程，TTL 到期后自动删除 key
    bool result = etcd_client_->register_service(service_name, host_address);
    if (result) {
        LOG_INFO("[TransmitServerBuilder] Service registered to etcd: {} -> {}", service_name, host_address);
    } else {
        LOG_ERROR("[TransmitServerBuilder] Failed to register service to etcd");
    }
    return result;
}

/**
 * @brief 启动 brpc 服务器
 * 
 * 在 listen_addr:listen_port 上启动服务，设置 idle_timeout_sec=-1
 * 表示永不因空闲断开客户端连接。
 */
bool TransmitServerBuilder::start() {
    if (!brpc_server_) {
        LOG_ERROR("[TransmitServerBuilder] BRPC server not initialized");
        return false;
    }

    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    brpc::ServerOptions options;
    // idle_timeout_sec = -1 表示永不因空闲断开客户端连接
    options.idle_timeout_sec = -1;

    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[TransmitServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }

    running_ = true;
    LOG_INFO("[TransmitServerBuilder] BRPC server started on {}", addr);
    return true;
}

/**
 * @brief 停止所有组件（优雅关闭）
 * 
 * 关闭顺序是启动的逆序，确保外部依赖先摘除、再停止内部服务：
 *   1. 先标记 running_=false，阻止健康检查等后台操作
 *   2. etcd 租约保活线程先停止，etcd 会在 TTL 后自动摘除该节点
 *   3. MQ 客户端停止，释放连接
 *   4. 信道池释放到下游服务的连接（避免下游等待超时）
 *   5. brpc 服务器优雅退出：
 *      - Stop(0): 立即停止接受新连接/新请求
 *      - Join():  阻塞等待所有正在处理的请求完成再退出
 */
void TransmitServerBuilder::stop() {
    running_ = false;

    // 关闭顺序：先停止外部依赖，最后停止 brpc 服务器
    // 1. 停止 etcd 租约保活（避免服务还对外可见）
    if (etcd_client_) {
        etcd_client_->stop();
        LOG_INFO("[TransmitServerBuilder] Etcd client stopped");
    }

    // 2. 停止 MQ 客户端（释放消息队列连接）
    if (mq_client_) {
        mq_client_->stop();
        LOG_INFO("[TransmitServerBuilder] MQ client stopped");
    }

    // 3. 停止 RPC 信道池（释放到下游服务的连接）
    if (channel_pool_) {
        channel_pool_->stop();
        LOG_INFO("[TransmitServerBuilder] Channel pool stopped");
    }

    // 4. 优雅停止 brpc 服务器
    //    Stop(0): 立即停止接受新请求，但不强制关闭已有连接
    //    Join():  等待所有正在处理的请求完成
    if (brpc_server_) {
        brpc_server_->Stop(0);
        brpc_server_->Join();
        LOG_INFO("[TransmitServerBuilder] BRPC server stopped");
    }
}

} // namespace transmit_service
