// =============================================================================
// message_server_builder.cc - 消息存储服务器建造者类实现
// =============================================================================
// 本文件实现 MessageServerBuilder 类的所有方法。
//
// 初始化顺序（build 方法依次调用）：
//   1. init_logger()        - 日志系统（最先初始化，后续组件依赖日志）
//   2. init_database()      - MySQL 数据库连接
//   3. init_es()            - Elasticsearch 客户端
//   4. init_mq_client()     - RabbitMQ 客户端（含交换机/队列声明）
//   5. init_channel_pool()  - RPC 信道池（etcd 服务发现）
//   6. init_brpc_server()   - brpc RPC 服务器（注册服务）
//   7. init_etcd_client()   - etcd 服务注册客户端
//
// 注意事项：
//   - MQ 客户端需要等待连接就绪（最多重试10次，100ms间隔）
//   - 交换机类型为 fanout，确保广播消息被所有消费者接收
//   - 任何组件初始化失败，build 方法会立即返回 false
// =============================================================================

#include "message_server_builder.hpp"

#include <odb/mysql/database.hxx>
#include <thread>

namespace message_service {

// ==================== 构造与析构函数 ====================

/**
 * @brief 构造函数
 *
 * 设置所有配置项的默认值，确保即使不通过链式配置也能正常运行。
 */
MessageServerBuilder::MessageServerBuilder()
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    // 网络默认配置
    config_.listen_addr = "0.0.0.0";
    config_.listen_port = 10004;
    config_.external_addr = "";
    config_.external_port = 0;

    // etcd 默认配置
    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;

    // MySQL 默认配置
    config_.mysql_user = "root";
    config_.mysql_password = "123456";
    config_.mysql_database = "chat_message";
    config_.mysql_host = "127.0.0.1";
    config_.mysql_port = 0;

    // ES 默认配置
    config_.es_host = "127.0.0.1";
    config_.es_port = 9200;

    // RabbitMQ 默认配置
    config_.mq_host = "127.0.0.1";
    config_.mq_port = 5672;
    config_.mq_user = "guest";
    config_.mq_password = "guest";
    config_.mq_vhost = "/";

    // 日志默认配置
    config_.is_debug = false;
    config_.log_file = "message_server.log";
}

MessageServerBuilder::~MessageServerBuilder() {
    stop();
}

// ==================== 配置方法实现（链式调用） ====================

MessageServerBuilder& MessageServerBuilder::with_listen_address(const std::string& addr) {
    config_.listen_addr = addr;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_listen_port(int port) {
    config_.listen_port = port;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_external_address(const std::string& addr) {
    config_.external_addr = addr;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_external_port(int port) {
    config_.external_port = port;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_etcd_lease_ttl(int ttl) {
    etcd_lease_ttl_ = ttl;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mysql_user(const std::string& user) {
    config_.mysql_user = user;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mysql_password(const std::string& password) {
    config_.mysql_password = password;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mysql_database(const std::string& database) {
    config_.mysql_database = database;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mysql_host(const std::string& host) {
    config_.mysql_host = host;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mysql_port(int port) {
    config_.mysql_port = port;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_es_host(const std::string& host) {
    config_.es_host = host;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_es_port(int port) {
    config_.es_port = port;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mq_host(const std::string& host) {
    config_.mq_host = host;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mq_port(int port) {
    config_.mq_port = port;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mq_user(const std::string& user) {
    config_.mq_user = user;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mq_password(const std::string& password) {
    config_.mq_password = password;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_mq_vhost(const std::string& vhost) {
    config_.mq_vhost = vhost;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file;
    return *this;
}

MessageServerBuilder& MessageServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level;
    return *this;
}

// ==================== 服务注册 ====================

/**
 * @brief 注册 protobuf RPC 服务
 *
 * 将服务指针保存到列表中，在 init_brpc_server 时统一添加到 brpc 服务器。
 */
MessageServerBuilder& MessageServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[MessageServerBuilder] BRPC service registered");
    return *this;
}

// ==================== 私有初始化方法 ====================

/**
 * @brief 初始化日志系统
 *
 * 最先初始化的组件，其他组件的日志依赖此系统。
 * 支持文件日志和控制台调试输出。
 */
bool MessageServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[MessageServerBuilder] Logger initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[MessageServerBuilder] Failed to init logger: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 初始化 MySQL 数据库连接
 *
 * 使用 ODB 的 MySQL 适配器创建数据库连接，
 * 并基于该连接构造 MessageTable 数据访问对象。
 */
bool MessageServerBuilder::init_database() {
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
                "utf8"  // 使用 UTF-8 编码
            )
        );
        // 构造消息表操作对象
        message_table_ = std::make_shared<message_table::MessageTable>(db);
        LOG_INFO("[MessageServerBuilder] Database initialized: {}@{}:{}/{}",
                 config_.mysql_user, config_.mysql_host, config_.mysql_port, config_.mysql_database);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[MessageServerBuilder] Failed to init database: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 Elasticsearch 客户端
 *
 * 用于文本消息的存储和关键字搜索。
 */
bool MessageServerBuilder::init_es() {
    try {
        message_es_ = std::make_shared<message_es::MessageES>(config_.es_host, config_.es_port);
        LOG_INFO("[MessageServerBuilder] ES client initialized: {}:{}", config_.es_host, config_.es_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[MessageServerBuilder] Failed to init ES: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 RabbitMQ 客户端
 *
 * 初始化流程：
 *   1. 创建 MQClient 实例
 *   2. 启动连接（异步）
 *   3. 等待连接就绪（最多 10 次重试，每次 100ms）
 *   4. 声明交换机和队列（fanout 类型，广播模式）
 *   5. 绑定队列到交换机
 *
 * 交换机说明：
 *   - 名称: message_exchange
 *   - 类型: fanout（广播，所有消费者都能收到消息）
 *   - 队列: message_storage_queue（消息存储服务专用队列）
 */
bool MessageServerBuilder::init_mq_client() {
    try {
        // 创建 MQ 客户端实例
        mq_client_ = std::make_shared<mq::MQClient>(
            config_.mq_host,
            static_cast<uint16_t>(config_.mq_port),
            config_.mq_user,
            config_.mq_password,
            config_.mq_vhost
        );

        // 启动 MQ 连接
        if (!mq_client_->start()) {
            LOG_ERROR("[MessageServerBuilder] Failed to start MQ client");
            return false;
        }

        // 等待 MQ 连接就绪，最多重试 10 次，每次间隔 100ms
        // 这是为了防止在 MQ 连接完全就绪前就声明交换机/队列导致失败
        int retry_count = 0;
        while (!mq_client_->is_connected() && retry_count < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            retry_count++;
            LOG_INFO("[MessageServerBuilder] Waiting for MQ connection... ({}/10)", retry_count);
        }

        if (!mq_client_->is_connected()) {
            LOG_ERROR("[MessageServerBuilder] MQ connection timeout");
            return false;
        }

        // 声明交换机和队列并绑定
        // 消息存储服务作为消费者，订阅消息转发服务发布的消息
        // 使用 fanout 交换机类型，确保所有消息都能被存储服务接收
        mq_client_->declareExchangeAndBind(
            "message_exchange",          // 交换机名称
            AMQP::fanout,                // 交换机类型（广播）
            "message_storage_queue",     // 队列名称
            ""                           // 路由键（fanout 模式下无效）
        );

        LOG_INFO("[MessageServerBuilder] MQ client initialized: {}:{}", config_.mq_host, config_.mq_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[MessageServerBuilder] Failed to init MQ client: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 RPC 信道池
 *
 * 基于 etcd 服务发现机制，自动发现 user_service 和 file_service 等下游服务，
 * 动态管理 RPC 连接信道。
 */
bool MessageServerBuilder::init_channel_pool() {
    try {
        channel_pool_ = std::make_shared<brpc::ServiceChannelPool>();
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        if (!channel_pool_->init_with_etcd(etcd_addr, "/services")) {
            LOG_ERROR("[MessageServerBuilder] Failed to init channel pool with etcd");
            return false;
        }
        LOG_INFO("[MessageServerBuilder] Channel pool initialized with etcd: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[MessageServerBuilder] Failed to init channel pool: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 brpc RPC 服务器
 *
 * 将之前注册的 protobuf 服务添加到 brpc 服务器中。
 * 注意：SERVER_DOESNT_OWN_SERVICE 表示 brpc 不负责释放服务对象。
 */
bool MessageServerBuilder::init_brpc_server() {
    try {
        brpc_server_ = std::make_unique<brpc::Server>();
        for (auto service : registered_services_) {
            if (brpc_server_->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
                LOG_ERROR("[MessageServerBuilder] Failed to add service");
                return false;
            }
        }
        LOG_INFO("[MessageServerBuilder] BRPC server initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[MessageServerBuilder] Failed to init BRPC server: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 etcd 客户端
 *
 * 用于将本服务注册到 etcd，供其他微服务发现和调用。
 */
bool MessageServerBuilder::init_etcd_client() {
    try {
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        etcd_client_ = std::make_unique<etcd::ServiceRegisterClient>(etcd_addr, etcd_lease_ttl_);
        LOG_INFO("[MessageServerBuilder] Etcd client initialized: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[MessageServerBuilder] Failed to init etcd client: {}", e.what());
        return false;
    }
}

// ==================== 构建所有组件 ====================

/**
 * @brief 按顺序构建所有组件
 *
 * 严格按照依赖顺序初始化，任何一步失败都会立即终止。
 * 初始化顺序：日志 → 数据库 → ES → MQ → 信道池 → brpc → etcd
 */
bool MessageServerBuilder::build() {
    if (!init_logger()) return false;
    if (!init_database()) return false;
    if (!init_es()) return false;
    if (!init_mq_client()) return false;
    if (!init_channel_pool()) return false;
    if (!init_brpc_server()) return false;
    if (!init_etcd_client()) return false;

    LOG_INFO("[MessageServerBuilder] All components built successfully");
    return true;
}

// ==================== 访问器方法 ====================

std::shared_ptr<message_table::MessageTable> MessageServerBuilder::get_message_table() const {
    return message_table_;
}

std::shared_ptr<message_es::MessageES> MessageServerBuilder::get_message_es() const {
    return message_es_;
}

std::shared_ptr<brpc::ServiceChannelPool> MessageServerBuilder::get_channel_pool() const {
    return channel_pool_;
}

std::shared_ptr<mq::MQClient> MessageServerBuilder::get_mq_client() const {
    return mq_client_;
}

std::unique_ptr<etcd::ServiceRegisterClient>& MessageServerBuilder::get_etcd_client() {
    return etcd_client_;
}

brpc::Server* MessageServerBuilder::get_brpc_server() {
    return brpc_server_.get();
}

// ==================== 运行时方法 ====================

/**
 * @brief 向 etcd 注册服务
 *
 * 优先使用外部地址（用于跨网段访问），
 * 若无外部地址则使用监听地址。
 */
bool MessageServerBuilder::register_service_to_etcd(const std::string& service_name) {
    if (!etcd_client_) {
        LOG_ERROR("[MessageServerBuilder] Etcd client not initialized");
        return false;
    }

    // 确定对外暴露的地址
    std::string host_address;
    if (!config_.external_addr.empty() && config_.external_port > 0) {
        host_address = config_.external_addr + ":" + std::to_string(config_.external_port);
    } else {
        host_address = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    }

    bool result = etcd_client_->register_service(service_name, host_address);
    if (result) {
        LOG_INFO("[MessageServerBuilder] Service registered to etcd: {} -> {}", service_name, host_address);
    } else {
        LOG_ERROR("[MessageServerBuilder] Failed to register service to etcd");
    }
    return result;
}

/**
 * @brief 启动 brpc RPC 服务器
 *
 * 监听配置的地址和端口，开始接受 RPC 请求。
 * idle_timeout_sec 设置为 -1 表示不超时。
 */
bool MessageServerBuilder::start() {
    if (!brpc_server_) {
        LOG_ERROR("[MessageServerBuilder] BRPC server not initialized");
        return false;
    }

    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;  // 连接不超时

    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[MessageServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }

    running_ = true;
    LOG_INFO("[MessageServerBuilder] BRPC server started on {}", addr);
    return true;
}

/**
 * @brief 停止服务器并释放所有资源
 *
 * 按照与初始化相反的顺序关闭：
 *   1. 停止 etcd 客户端（注销服务）
 *   2. 停止 MQ 客户端（断开连接）
 *   3. 停止信道池（关闭连接）
 *   4. 停止 brpc 服务器（等待请求完成）
 */
void MessageServerBuilder::stop() {
    running_ = false;

    if (etcd_client_) {
        etcd_client_->stop();
        LOG_INFO("[MessageServerBuilder] Etcd client stopped");
    }

    if (mq_client_) {
        mq_client_->stop();
        LOG_INFO("[MessageServerBuilder] MQ client stopped");
    }

    if (channel_pool_) {
        channel_pool_->stop();
        LOG_INFO("[MessageServerBuilder] Channel pool stopped");
    }

    if (brpc_server_) {
        brpc_server_->Stop(0);   // 等待 0 秒后强制停止
        brpc_server_->Join();    // 等待所有请求处理完成
        LOG_INFO("[MessageServerBuilder] BRPC server stopped");
    }
}

} // namespace message_service