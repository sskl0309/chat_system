// =============================================================================
// user_server_builder.cc - 用户服务器建造者类实现
// =============================================================================

#include "user_server_builder.hpp"

#include <odb/mysql/database.hxx>

namespace user_service {

// =============================================================================
// 构造与析构函数
// =============================================================================

UserServerBuilder::UserServerBuilder()
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    // ---------- 网络配置 ----------
    config_.listen_addr = "0.0.0.0";       // 监听所有网卡
    config_.listen_port = 10001;           // 默认监听端口
    config_.external_addr = "";            // 留空则用 listen_addr 替代
    config_.external_port = 0;             // 留空则用 listen_port 替代

    // ---------- etcd 配置 ----------
    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;              // etcd 默认端口

    // ---------- MySQL 配置 ----------
    config_.mysql_user = "root";
    config_.mysql_password = "123456";
    config_.mysql_database = "chat_user";
    config_.mysql_host = "127.0.0.1";
    config_.mysql_port = 0;                // 0 = 使用 MySQL 默认端口 3306

    // ---------- Redis 配置 ----------
    config_.redis_host = "127.0.0.1";
    config_.redis_port = 6379;

    // ---------- ES 配置 ----------
    config_.es_host = "127.0.0.1";
    config_.es_port = 9200;

    // ---------- 邮件配置 ----------
    config_.smtp_server = "smtp.qq.com";
    config_.smtp_port = 587;               // QQ 邮箱 STARTTLS 端口

    // ---------- 日志配置 ----------
    config_.is_debug = false;
    config_.log_file = "user_server.log";
}

UserServerBuilder::~UserServerBuilder() {
    stop();
}

// =============================================================================
// 配置方法实现
// =============================================================================

UserServerBuilder& UserServerBuilder::with_listen_address(const std::string& addr) {
    config_.listen_addr = addr;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_listen_port(int port) {
    config_.listen_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_external_address(const std::string& addr) {
    config_.external_addr = addr;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_external_port(int port) {
    config_.external_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_etcd_lease_ttl(int ttl) {
    etcd_lease_ttl_ = ttl;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_mysql_user(const std::string& user) {
    config_.mysql_user = user;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_mysql_password(const std::string& password) {
    config_.mysql_password = password;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_mysql_database(const std::string& database) {
    config_.mysql_database = database;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_mysql_host(const std::string& host) {
    config_.mysql_host = host;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_mysql_port(int port) {
    config_.mysql_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_redis_host(const std::string& host) {
    config_.redis_host = host;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_redis_port(int port) {
    config_.redis_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_es_host(const std::string& host) {
    config_.es_host = host;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_es_port(int port) {
    config_.es_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_smtp_server(const std::string& server) {
    config_.smtp_server = server;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_smtp_port(int port) {
    config_.smtp_port = port;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_sender_email(const std::string& email) {
    config_.sender_email = email;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_email_auth_code(const std::string& code) {
    config_.email_auth_code = code;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file;
    return *this;
}

UserServerBuilder& UserServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level;
    return *this;
}

// =============================================================================
// 服务注册
// =============================================================================

UserServerBuilder& UserServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[UserServerBuilder] BRPC service registered");
    return *this;
}

// =============================================================================
// 私有初始化方法
// =============================================================================

// 初始化日志系统
// 日志系统必须最先初始化，因为后续所有组件初始化都可能产生日志输出。
// 注意：日志初始化失败的日志必须通过 std::cerr 输出（此时日志系统还不可用）。
bool UserServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[UserServerBuilder] Logger initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[UserServerBuilder] Failed to init logger: " << e.what() << std::endl;
        return false;
    }
}

// 初始化 MySQL 数据库连接
// 使用 ODB ORM 框架的 MySQL 后端，创建 odb::database 对象后注入 UserTable。
// 连接池由 ODB 内部管理（线程安全），整个服务生命周期共享同一个 database 实例。
// 设计考量：
//   - port=0 时 ODB 使用 MySQL 默认端口 3306
//   - socket 传 nullptr 表示使用 TCP 连接（而非 Unix domain socket）
//   - charset=utf8 确保中文数据正确存储
bool UserServerBuilder::init_database() {
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
        // 将数据库实例注入 UserTable，后续所有 CRUD 操作共享此连接
        user_table_ = std::make_shared<user_table::UserTable>(db);
        LOG_INFO("[UserServerBuilder] Database initialized: {}@{}:{}/{}",
                 config_.mysql_user, config_.mysql_host, config_.mysql_port, config_.mysql_database);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init database: {}", e.what());
        return false;
    }
}

// 初始化 Redis 客户端
// Redis 用于会话管理（session_id -> user_id 映射）和验证码临时存储。
// 会话数据和验证码均设置 TTL 过期时间，无需显式清理。
// 注意：RedisClient 连接在构造时建立，不支持自动重连（依赖 hiredis 库自动重连机制）。
bool UserServerBuilder::init_redis() {
    try {
        redis_client_ = std::make_shared<redis_client::RedisClient>(config_.redis_host, config_.redis_port);
        LOG_INFO("[UserServerBuilder] Redis client initialized: {}:{}", config_.redis_host, config_.redis_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init Redis: {}", e.what());
        return false;
    }
}

// 初始化邮件客户端
// 使用 SMTP 协议发送验证码邮件，连接参数由配置决定。
// 当前实现支持 QQ 邮箱（smtp.qq.com:587），使用 STARTTLS 加密。
// 授权码（auth_code）与邮箱登录密码不同，需在 QQ 邮箱设置中生成。
bool UserServerBuilder::init_email_client() {
    try {
        email_client_ = std::make_shared<email_client::EmailClient>(
            config_.smtp_server, config_.smtp_port, config_.sender_email, config_.email_auth_code);
        LOG_INFO("[UserServerBuilder] Email client initialized: {}", config_.sender_email);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init email client: {}", e.what());
        return false;
    }
}

// 初始化 Elasticsearch 客户端
// ES 用于用户搜索功能（按昵称/用户ID/邮箱搜索）。
// 启动时调用 create_index() 创建索引，如果索引已存在则跳过（幂等操作）。
// 索引的 nickname 字段使用 ik_max_word 中文分词器，支持中文模糊搜索。
bool UserServerBuilder::init_es() {
    try {
        user_es_ = std::make_shared<user_es::UserES>(config_.es_host, config_.es_port);
        // 尝试创建索引（已存在则跳过，底层 ES 返回 400 时 ignore 处理）
        user_es_->create_index();
        LOG_INFO("[UserServerBuilder] ES client initialized: {}:{}", config_.es_host, config_.es_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init ES: {}", e.what());
        return false;
    }
}

// 初始化 RPC 信道池
// ServiceChannelPool 基于 etcd 自动发现下游服务（目前主要是 file_service）。
// init_with_etcd 会在 /services 前缀下 watch 服务注册变更，
// 当有新服务实例上线或下线时，自动更新信道池中的连接。
// 这样用户服务在获取头像时，无需硬编码文件服务地址，支持动态扩缩容。
bool UserServerBuilder::init_channel_pool() {
    try {
        // ServiceChannelPool 基于 etcd 自动发现下游服务
        // init_with_etcd 会监听 /services 前缀下的服务注册变更，动态维护信道池
        channel_pool_ = std::make_shared<brpc::ServiceChannelPool>();
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        if (!channel_pool_->init_with_etcd(etcd_addr, "/services")) {
            LOG_ERROR("[UserServerBuilder] Failed to init channel pool with etcd");
            return false;
        }
        LOG_INFO("[UserServerBuilder] Channel pool initialized with etcd: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init channel pool: {}", e.what());
        return false;
    }
}

// 初始化 brpc 服务器
// 创建 brpc::Server 实例并绑定所有已注册的 protobuf service。
// SERVER_DOESNT_OWN_SERVICE 标志表示 brpc::Server 不负责释放 service 对象，
// service 的生命周期由调用方（main 函数中的 service_impl 栈对象）管理。
bool UserServerBuilder::init_brpc_server() {
    try {
        brpc_server_ = std::make_unique<brpc::Server>();
        for (auto service : registered_services_) {
            if (brpc_server_->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
                LOG_ERROR("[UserServerBuilder] Failed to add service");
                return false;
            }
        }
        LOG_INFO("[UserServerBuilder] BRPC server initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init BRPC server: {}", e.what());
        return false;
    }
}

// 初始化 etcd 服务注册客户端
// etcd 客户端负责将本服务实例注册到 etcd、维持租约保活。
// 租约 TTL 后如未续约，etcd 会自动删除该 key，实现故障自动摘除。
// 初始化放在最后，因为信道池（init_channel_pool）也需要访问 etcd。
bool UserServerBuilder::init_etcd_client() {
    try {
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        etcd_client_ = std::make_unique<etcd::ServiceRegisterClient>(etcd_addr, etcd_lease_ttl_);
        LOG_INFO("[UserServerBuilder] Etcd client initialized: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[UserServerBuilder] Failed to init etcd client: {}", e.what());
        return false;
    }
}

// =============================================================================
// 构建所有组件
// =============================================================================

// 构建所有组件
// 按严格的依赖顺序初始化，短路失败：任一步骤失败立即返回 false。
// 初始化顺序的依据：
//   1. 日志：必须在最前面，以便后续组件输出错误日志
//   2-5. 数据存储与外部服务：无相互依赖，可任意排列
//   6. 信道池：依赖 etcd 用于服务发现（但使用独立连接）
//   7. brpc：需在所有依赖组件就绪后才能启动
//   8. etcd 注册客户端：最后初始化，待服务器完全就绪后才注册（避免流量过早路由）
bool UserServerBuilder::build() {
    // 按依赖顺序初始化：日志最先，etcd 最后（其他组件可能依赖日志输出）
    if (!init_logger()) return false;          // 1. 日志系统（最优先）
    if (!init_database()) return false;        // 2. MySQL 数据库
    if (!init_redis()) return false;           // 3. Redis 缓存
    if (!init_email_client()) return false;    // 4. 邮件客户端
    if (!init_es()) return false;              // 5. ES 搜索服务
    if (!init_channel_pool()) return false;    // 6. RPC 信道池（etcd 服务发现）
    if (!init_brpc_server()) return false;     // 7. brpc 服务器
    if (!init_etcd_client()) return false;     // 8. etcd 注册客户端

    LOG_INFO("[UserServerBuilder] All components built successfully");
    return true;
}

// =============================================================================
// 访问器方法
// =============================================================================

std::shared_ptr<user_table::UserTable> UserServerBuilder::get_user_table() const {
    return user_table_;
}

std::shared_ptr<redis_client::RedisClient> UserServerBuilder::get_redis_client() const {
    return redis_client_;
}

std::shared_ptr<email_client::EmailClient> UserServerBuilder::get_email_client() const {
    return email_client_;
}

std::shared_ptr<user_es::UserES> UserServerBuilder::get_user_es() const {
    return user_es_;
}

std::shared_ptr<brpc::ServiceChannelPool> UserServerBuilder::get_channel_pool() const {
    return channel_pool_;
}

std::unique_ptr<etcd::ServiceRegisterClient>& UserServerBuilder::get_etcd_client() {
    return etcd_client_;
}

brpc::Server* UserServerBuilder::get_brpc_server() {
    return brpc_server_.get();
}

// =============================================================================
// 运行时方法
// =============================================================================

bool UserServerBuilder::register_service_to_etcd(const std::string& service_name) {
    if (!etcd_client_) {
        LOG_ERROR("[UserServerBuilder] Etcd client not initialized");
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
        LOG_INFO("[UserServerBuilder] Service registered to etcd: {} -> {}", service_name, host_address);
    } else {
        LOG_ERROR("[UserServerBuilder] Failed to register service to etcd");
    }
    return result;
}

bool UserServerBuilder::start() {
    if (!brpc_server_) {
        LOG_ERROR("[UserServerBuilder] BRPC server not initialized");
        return false;
    }

    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    brpc::ServerOptions options;
    // idle_timeout_sec = -1 表示永不因空闲断开客户端连接
    options.idle_timeout_sec = -1;

    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[UserServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }

    running_ = true;
    LOG_INFO("[UserServerBuilder] BRPC server started on {}", addr);
    return true;
}

// 停止所有组件（优雅关闭）
// 关闭顺序是启动的逆序，确保外部依赖先摘除、再停止内部服务：
//   1. 先标记 running_=false，阻止健康检查等后台操作
//   2. etcd 租约保活线程先停止，etcd 会在 TTL 后自动摘除该节点
//   3. 信道池释放到下游服务的连接（避免下游等待超时）
//   4. brpc 服务器优雅退出：
//      - Stop(0): 立即停止接受新连接/新请求
//      - Join():  阻塞等待所有正在处理的请求完成再退出
// 注意：etcd_client_ 和 channel_pool_ 在 brpc_server_ 之前停止，
//       避免 brpc 处理中的请求尝试调用已关闭的下游或注册已摘除的服务。
void UserServerBuilder::stop() {
    running_ = false;

    // 关闭顺序：先停止外部依赖，最后停止 brpc 服务器
    // 1. 停止 etcd 租约保活（避免服务还对外可见）
    if (etcd_client_) {
        etcd_client_->stop();
        LOG_INFO("[UserServerBuilder] Etcd client stopped");
    }

    // 2. 停止 RPC 信道池（释放到下游服务的连接）
    if (channel_pool_) {
        channel_pool_->stop();
        LOG_INFO("[UserServerBuilder] Channel pool stopped");
    }

    // 3. 优雅停止 brpc 服务器
    //    Stop(0): 立即停止接受新请求，但不强制关闭已有连接
    //    Join():  等待所有正在处理的请求完成
    if (brpc_server_) {
        brpc_server_->Stop(0);
        brpc_server_->Join();
        LOG_INFO("[UserServerBuilder] BRPC server stopped");
    }
}

} // namespace user_service
