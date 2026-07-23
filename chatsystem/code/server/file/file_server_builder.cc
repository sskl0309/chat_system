// =============================================================================
// file_server_builder.cc - 文件服务器建造者类实现
// =============================================================================
// 本文件实现 file_server_builder.hpp 中声明的 FileServerBuilder 类。
// 涵盖所有配置方法、初始化方法、运行时方法的实现。
//
// 实现要点：
//   - 所有初始化方法返回 bool，便于 build() 中按顺序检查
//   - 异常处理使用 try-catch 包裹关键操作
//   - 日志记录覆盖所有关键节点
// =============================================================================

#include "file_server_builder.hpp"

namespace file {

// =============================================================================
// 构造与析构函数实现
// =============================================================================

/**
 * @brief 构造函数实现
 *
 * 初始化所有配置参数为默认值：
 *   - 监听地址：0.0.0.0（监听所有网络接口）
 *   - 监听端口：10000（避免与语音服务 8888 冲突）
 *   - 存储目录：./file_storage
 *   - etcd地址：127.0.0.1
 *   - etcd端口：2379
 *   - 租约时长：30秒
 *   - 日志级别：INFO
 *   - 运行状态：false
 */
FileServerBuilder::FileServerBuilder()
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    config_.listen_addr = "0.0.0.0";
    config_.listen_port = 10000;
    config_.storage_dir = "./file_storage";
    config_.external_addr = "";
    config_.external_port = 0;
    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;
    config_.is_debug = false;
    config_.log_file = "file_server.log";
}

/**
 * @brief 析构函数实现
 *
 * 自动调用 stop() 方法释放资源，确保资源被正确清理。
 */
FileServerBuilder::~FileServerBuilder() {
    stop();
}

// =============================================================================
// 配置方法实现（链式调用）
// =============================================================================

FileServerBuilder& FileServerBuilder::with_listen_address(const std::string& addr) {
    config_.listen_addr = addr;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_listen_port(int port) {
    config_.listen_port = port;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_storage_dir(const std::string& dir) {
    config_.storage_dir = dir;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_external_address(const std::string& addr) {
    config_.external_addr = addr;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_external_port(int port) {
    config_.external_port = port;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_etcd_lease_ttl(int ttl) {
    etcd_lease_ttl_ = ttl;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file;
    return *this;
}

FileServerBuilder& FileServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level;
    return *this;
}

// =============================================================================
// 服务注册与构建方法实现
// =============================================================================

/**
 * @brief 注册 brpc 服务实现实现
 *
 * 将 brpc 服务实现对象添加到已注册服务列表，在 build() 时统一注册到服务器。
 */
FileServerBuilder& FileServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[FileServerBuilder] BRPC service registered");
    return *this;
}

// =============================================================================
// 私有初始化方法实现
// =============================================================================

/**
 * @brief 初始化日志模块实现
 */
bool FileServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[FileServerBuilder] Logger initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FileServerBuilder] Failed to init logger: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 初始化文件存储实现
 *
 * 使用 LocalFileStorage 作为具体存储实现。
 * 构造时 LocalFileStorage 会自动创建存储目录。
 */
bool FileServerBuilder::init_storage() {
    try {
        storage_ = std::make_shared<LocalFileStorage>(config_.storage_dir);
        LOG_INFO("[FileServerBuilder] File storage initialized, dir: {}", config_.storage_dir);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FileServerBuilder] Failed to init storage: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 brpc 服务器实现
 *
 * 创建 brpc 服务器实例，并将所有已注册的服务实现添加到服务器中。
 * 使用 SERVER_DOESNT_OWN_SERVICE 标志，表示服务器不管理服务对象的生命周期。
 */
bool FileServerBuilder::init_brpc_server() {
    try {
        brpc_server_ = std::make_unique<brpc::Server>();

        // 遍历所有已注册的服务实现，添加到服务器中
        for (auto service : registered_services_) {
            if (brpc_server_->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
                LOG_ERROR("[FileServerBuilder] Failed to add service");
                return false;
            }
        }

        LOG_INFO("[FileServerBuilder] BRPC server initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FileServerBuilder] Failed to init BRPC server: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 etcd 客户端实现
 */
bool FileServerBuilder::init_etcd_client() {
    try {
        std::string etcd_addr = config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        etcd_client_ = std::make_unique<etcd::ServiceRegisterClient>(etcd_addr, etcd_lease_ttl_);
        LOG_INFO("[FileServerBuilder] Etcd client initialized: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[FileServerBuilder] Failed to init etcd client: {}", e.what());
        return false;
    }
}

/**
 * @brief 构建所有组件实现
 *
 * 按顺序初始化：
 *   1. 日志模块（必须最先初始化）
 *   2. 文件存储
 *   3. brpc 服务器
 *   4. etcd 客户端
 *
 * 任何组件失败都会立即返回 false。
 */
bool FileServerBuilder::build() {
    if (!init_logger()) {
        return false;
    }

    if (!init_storage()) {
        return false;
    }

    if (!init_brpc_server()) {
        return false;
    }

    if (!init_etcd_client()) {
        return false;
    }

    LOG_INFO("[FileServerBuilder] All components built successfully");
    return true;
}

// =============================================================================
// 访问器方法实现
// =============================================================================

FileStoragePtr FileServerBuilder::get_storage() const {
    return storage_;
}

std::unique_ptr<etcd::ServiceRegisterClient>& FileServerBuilder::get_etcd_client() {
    return etcd_client_;
}

brpc::Server* FileServerBuilder::get_brpc_server() {
    return brpc_server_.get();
}

// =============================================================================
// 运行时方法实现
// =============================================================================

/**
 * @brief 向 etcd 注册服务实现
 *
 * 优先使用外部访问地址进行注册（适用于云环境），
 * 如果未设置外部地址则使用监听地址。
 */
bool FileServerBuilder::register_service_to_etcd(const std::string& service_name) {
    if (!etcd_client_) {
        LOG_ERROR("[FileServerBuilder] Etcd client not initialized");
        return false;
    }

    // 确定注册地址：优先使用外部地址，否则使用监听地址
    std::string host_address;
    if (!config_.external_addr.empty() && config_.external_port > 0) {
        host_address = config_.external_addr + ":" + std::to_string(config_.external_port);
    } else {
        host_address = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    }

    bool result = etcd_client_->register_service(service_name, host_address);
    if (result) {
        LOG_INFO("[FileServerBuilder] Service registered to etcd: {} -> {}",
                 service_name, host_address);
    } else {
        LOG_ERROR("[FileServerBuilder] Failed to register service to etcd");
    }

    return result;
}

/**
 * @brief 启动 brpc 服务器实现
 *
 * 在指定地址和端口上启动 brpc 服务监听，设置空闲连接超时为 -1（不超时）。
 */
bool FileServerBuilder::start() {
    if (!brpc_server_) {
        LOG_ERROR("[FileServerBuilder] BRPC server not initialized");
        return false;
    }

    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;  // 禁用空闲连接超时

    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[FileServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }

    running_ = true;
    LOG_INFO("[FileServerBuilder] BRPC server started on {}", addr);
    return true;
}

/**
 * @brief 停止服务器实现
 *
 * 按顺序释放资源：
 *   1. 设置运行状态为 false
 *   2. 停止 etcd 保活线程（释放 Lease）
 *   3. 停止 brpc 服务器（停止监听并等待线程退出）
 */
void FileServerBuilder::stop() {
    running_ = false;

    // 停止 etcd 保活
    if (etcd_client_) {
        etcd_client_->stop();
        LOG_INFO("[FileServerBuilder] Etcd client stopped");
    }

    // 停止 brpc 服务器
    if (brpc_server_) {
        brpc_server_->Stop(0);   // 立即停止
        brpc_server_->Join();    // 等待线程退出
        LOG_INFO("[FileServerBuilder] BRPC server stopped");
    }
}

} // namespace file
