// =============================================================================
// gateway_server_builder.cc - 网关服务器建造者类实现
// =============================================================================
// 本文件实现 GatewayServerBuilder 类的所有方法，负责按顺序初始化
// 网关服务器的各个组件并管理其生命周期。
//
// 初始化顺序：
//   1. 日志系统（最先初始化，后续组件需要日志输出）
//   2. Redis客户端（会话鉴权）
//   3. RPC信道池（etcd服务发现，依赖etcd）
//   4. 网关服务器（HTTP + WebSocket，依赖上述所有组件）
// =============================================================================

#include "gateway_server_builder.hpp"

namespace gateway {

// =============================================================================
// 构造与析构
// =============================================================================

GatewayServerBuilder::GatewayServerBuilder()
    : log_level_(mylog::LogLevel::INFO), running_(false) {
    // 设置默认配置
    config_.http_listen_addr = "0.0.0.0";
    config_.http_port = 8888;

    config_.ws_listen_addr = "0.0.0.0";
    config_.ws_port = 8889;

    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;

    config_.redis_host = "127.0.0.1";
    config_.redis_port = 6379;
    config_.redis_db = 0;

    config_.is_debug = false;
    config_.log_file = "gateway_server.log";
}

GatewayServerBuilder::~GatewayServerBuilder() {
    stop();
}

// =============================================================================
// 配置方法实现
// =============================================================================

GatewayServerBuilder& GatewayServerBuilder::with_http_listen_address(const std::string& addr) {
    config_.http_listen_addr = addr;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_http_port(int port) {
    config_.http_port = port;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_ws_listen_address(const std::string& addr) {
    config_.ws_listen_addr = addr;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_ws_port(int port) {
    config_.ws_port = port;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_redis_host(const std::string& host) {
    config_.redis_host = host;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_redis_port(int port) {
    config_.redis_port = port;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_redis_db(int db) {
    config_.redis_db = db;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level;
    return *this;
}

// =============================================================================
// 私有初始化方法
// =============================================================================

// 初始化日志系统
// 日志系统必须最先初始化，因为后续所有组件初始化都可能产生日志输出。
bool GatewayServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[GatewayServerBuilder] Logger initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[GatewayServerBuilder] Failed to init logger: " << e.what() << std::endl;
        return false;
    }
}

// 初始化Redis客户端
// Redis用于会话鉴权：根据session_id查找对应的user_id，验证登录状态。
bool GatewayServerBuilder::init_redis() {
    try {
        redis_client_ = std::make_shared<redis_client::RedisClient>(
            config_.redis_host, config_.redis_port, config_.redis_db);
        LOG_INFO("[GatewayServerBuilder] Redis client initialized: {}:{}/db{}",
                 config_.redis_host, config_.redis_port, config_.redis_db);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[GatewayServerBuilder] Failed to init Redis: {}", e.what());
        return false;
    }
}

// 初始化RPC信道池
// ServiceChannelPool基于etcd自动发现所有子服务（用户、好友、消息、文件、语音等）。
// init_with_etcd会在/services前缀下watch服务注册变更，动态维护信道池。
bool GatewayServerBuilder::init_channel_pool() {
    try {
        channel_pool_ = std::make_shared<brpc::ServiceChannelPool>();
        std::string etcd_addr = "http://" + config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        if (!channel_pool_->init_with_etcd(etcd_addr, "/services")) {
            LOG_ERROR("[GatewayServerBuilder] Failed to init channel pool with etcd");
            return false;
        }
        LOG_INFO("[GatewayServerBuilder] Channel pool initialized with etcd: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[GatewayServerBuilder] Failed to init channel pool: {}", e.what());
        return false;
    }
}

// 初始化网关服务器
// 将Redis客户端和RPC信道池注入网关服务器，注册所有HTTP路由和WebSocket处理。
bool GatewayServerBuilder::init_gateway_server() {
    try {
        gateway_server_.init(config_.http_port, config_.ws_port,
                             redis_client_, channel_pool_);
        LOG_INFO("[GatewayServerBuilder] Gateway server initialized: HTTP={}, WS={}",
                 config_.http_port, config_.ws_port);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[GatewayServerBuilder] Failed to init gateway server: {}", e.what());
        return false;
    }
}

// =============================================================================
// 构建与运行
// =============================================================================

// 按依赖顺序初始化所有组件
//   1. 日志系统（最优先，后续组件需要日志输出）
//   2. Redis客户端（会话鉴权）
//   3. RPC信道池（etcd服务发现）
//   4. 网关服务器（HTTP + WebSocket）
bool GatewayServerBuilder::build() {
    if (!init_logger()) return false;
    if (!init_redis()) return false;
    if (!init_channel_pool()) return false;
    if (!init_gateway_server()) return false;

    LOG_INFO("[GatewayServerBuilder] All components built successfully");
    return true;
}

// 启动网关服务器
bool GatewayServerBuilder::start() {
    if (running_) {
        LOG_WARN("[GatewayServerBuilder] Already running");
        return true;
    }

    if (!gateway_server_.start()) {
        LOG_ERROR("[GatewayServerBuilder] Failed to start gateway server");
        return false;
    }

    running_ = true;
    LOG_INFO("[GatewayServerBuilder] Gateway server started successfully");
    return true;
}

// 停止网关服务器
void GatewayServerBuilder::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    gateway_server_.stop();
    LOG_INFO("[GatewayServerBuilder] Gateway server stopped");
}

// =============================================================================
// 访问器方法
// =============================================================================

std::shared_ptr<redis_client::RedisClient> GatewayServerBuilder::get_redis_client() const {
    return redis_client_;
}

std::shared_ptr<brpc::ServiceChannelPool> GatewayServerBuilder::get_channel_pool() const {
    return channel_pool_;
}

GatewayServerImpl* GatewayServerBuilder::get_gateway_server() {
    return &gateway_server_;
}

} // namespace gateway
