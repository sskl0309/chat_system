#include "speech_server_builder.h"

namespace speech {

/**
 * @brief 构造函数实现
 * 
 * 初始化所有配置参数为默认值：
 * - 监听地址：0.0.0.0（监听所有网络接口）
 * - 监听端口：8080
 * - etcd地址：127.0.0.1
 * - etcd端口：2379
 * - 租约时长：30秒
 * - 日志级别：INFO
 * - 运行状态：false
 */
SpeechServerBuilder::SpeechServerBuilder() 
    : etcd_lease_ttl_(30), log_level_(mylog::LogLevel::INFO), running_(false) {
    config_.listen_addr = "0.0.0.0";
    config_.listen_port = 8080;
    config_.external_addr = "";
    config_.external_port = 0;
    config_.etcd_addr = "127.0.0.1";
    config_.etcd_port = 2379;
    config_.is_debug = false;
    config_.log_file = "speech_server.log";
}

/**
 * @brief 析构函数实现
 * 
 * 自动调用 stop() 方法释放资源，确保资源被正确清理。
 */
SpeechServerBuilder::~SpeechServerBuilder() {
    stop();
}

/**
 * @brief 设置服务监听地址实现
 * 
 * @param addr 监听地址字符串
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_listen_address(const std::string& addr) {
    config_.listen_addr = addr;
    return *this;
}

/**
 * @brief 设置服务监听端口实现
 * 
 * @param port 监听端口号
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_listen_port(int port) {
    config_.listen_port = port;
    return *this;
}

/**
 * @brief 设置外部访问地址实现
 * 
 * 当服务器部署在云环境中，监听地址（内网）与外部访问地址（公网）不一致时使用。
 * 
 * @param addr 外部访问地址字符串
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_external_address(const std::string& addr) {
    config_.external_addr = addr;
    return *this;
}

/**
 * @brief 设置外部访问端口实现
 * 
 * @param port 外部访问端口号
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_external_port(int port) {
    config_.external_port = port;
    return *this;
}

/**
 * @brief 设置 etcd 服务器地址实现
 * 
 * @param addr etcd 服务器地址字符串
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_etcd_address(const std::string& addr) {
    config_.etcd_addr = addr;
    return *this;
}

/**
 * @brief 设置 etcd 服务器端口实现
 * 
 * @param port etcd 服务器端口号
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_etcd_port(int port) {
    config_.etcd_port = port;
    return *this;
}

/**
 * @brief 设置 etcd Lease 租约时长实现
 * 
 * Lease 用于服务注册的自动过期机制，当服务下线或网络中断时，
 * 租约过期后服务会自动从 etcd 中删除，实现自动故障剔除。
 * 
 * @param ttl 租约时长（秒）
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_etcd_lease_ttl(int ttl) {
    etcd_lease_ttl_ = ttl;
    return *this;
}

/**
 * @brief 设置百度语音识别凭证实现
 * 
 * @param app_id 百度AI开放平台 AppID
 * @param api_key API Key，用于获取访问令牌
 * @param secret_key Secret Key，用于获取访问令牌
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_speech_credentials(const std::string& app_id,
                                                                  const std::string& api_key,
                                                                  const std::string& secret_key) {
    config_.app_id = app_id;
    config_.api_key = api_key;
    config_.secret_key = secret_key;
    return *this;
}

/**
 * @brief 设置调试模式实现
 * 
 * @param debug true 开启调试模式（日志输出到控制台），false 关闭调试模式（日志输出到文件）
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_debug_mode(bool debug) {
    config_.is_debug = debug;
    return *this;
}

/**
 * @brief 设置日志文件路径实现
 * 
 * @param file 日志文件路径
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_log_file(const std::string& file) {
    config_.log_file = file;
    return *this;
}

/**
 * @brief 设置日志输出级别实现
 * 
 * @param level 日志级别枚举值
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::with_log_level(mylog::LogLevel level) {
    log_level_ = level;
    return *this;
}

/**
 * @brief 初始化日志模块实现
 * 
 * 根据配置的调试模式和日志级别初始化 spdlog 日志系统。
 * 调试模式下日志输出到控制台，生产模式下日志输出到文件。
 * 
 * @return bool 初始化成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::init_logger() {
    try {
        mylog::init(config_.is_debug, config_.log_file, log_level_);
        LOG_INFO("[SpeechServerBuilder] Logger initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[SpeechServerBuilder] Failed to init logger: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 初始化语音识别客户端实现
 * 
 * 使用配置的百度语音识别凭证创建客户端实例。
 * 如果凭证未设置（为空），则初始化失败。
 * 
 * @return bool 初始化成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::init_speech_client() {
    // 检查凭证是否已设置
    if (config_.app_id.empty() || config_.api_key.empty() || config_.secret_key.empty()) {
        LOG_ERROR("[SpeechServerBuilder] Speech credentials not set");
        return false;
    }

    try {
        speech_client_ = std::make_shared<SpeechClient>(
            config_.app_id, config_.api_key, config_.secret_key);
        LOG_INFO("[SpeechServerBuilder] SpeechClient initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[SpeechServerBuilder] Failed to init SpeechClient: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 brpc 服务器实现
 * 
 * 创建 brpc 服务器实例，并将所有已注册的服务实现添加到服务器中。
 * 使用 SERVER_DOESNT_OWN_SERVICE 标志，表示服务器不负责管理服务对象的生命周期。
 * 
 * @return bool 初始化成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::init_brpc_server() {
    try {
        // 创建 brpc 服务器智能指针
        brpc_server_ = std::make_unique<brpc::Server>();

        // 遍历所有已注册的服务实现，添加到服务器中
        for (auto service : registered_services_) {
            if (brpc_server_->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
                LOG_ERROR("[SpeechServerBuilder] Failed to add service");
                return false;
            }
        }

        LOG_INFO("[SpeechServerBuilder] BRPC server initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[SpeechServerBuilder] Failed to init BRPC server: {}", e.what());
        return false;
    }
}

/**
 * @brief 初始化 etcd 客户端实现
 * 
 * 创建 etcd 服务注册客户端实例，用于向 etcd 注册服务并保持保活。
 * 
 * @return bool 初始化成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::init_etcd_client() {
    try {
        // 拼接 etcd 服务器地址（格式：ip:port）
        std::string etcd_addr = config_.etcd_addr + ":" + std::to_string(config_.etcd_port);
        // 创建 etcd 服务注册客户端，指定租约时长
        etcd_client_ = std::make_unique<etcd::ServiceRegisterClient>(etcd_addr, etcd_lease_ttl_);
        LOG_INFO("[SpeechServerBuilder] Etcd client initialized successfully: {}", etcd_addr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[SpeechServerBuilder] Failed to init Etcd client: {}", e.what());
        return false;
    }
}

/**
 * @brief 构建所有组件实现
 * 
 * 按顺序初始化以下组件：
 * 1. 日志模块（必须最先初始化，后续组件依赖日志）
 * 2. 语音识别客户端
 * 3. brpc 服务器（包含所有服务实现）
 * 4. etcd 客户端（用于服务注册）
 * 
 * 任何组件初始化失败都会立即返回 false，确保所有组件都成功初始化。
 * 
 * @return bool 构建成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::build() {
    // 1. 初始化日志模块（必须最先初始化）
    if (!init_logger()) {
        return false;
    }

    // 2. 初始化语音识别客户端
    if (!init_speech_client()) {
        return false;
    }

    // 3. 初始化 brpc 服务器
    if (!init_brpc_server()) {
        return false;
    }

    // 4. 初始化 etcd 客户端
    if (!init_etcd_client()) {
        return false;
    }

    LOG_INFO("[SpeechServerBuilder] All components built successfully");
    return true;
}

/**
 * @brief 获取语音识别客户端实现
 * 
 * @return std::shared_ptr<SpeechClient> 语音识别客户端智能指针
 */
std::shared_ptr<SpeechClient> SpeechServerBuilder::get_speech_client() const {
    return speech_client_;
}

/**
 * @brief 获取 etcd 注册客户端实现
 * 
 * @return std::unique_ptr<etcd::ServiceRegisterClient>& etcd 注册客户端引用
 */
std::unique_ptr<etcd::ServiceRegisterClient>& SpeechServerBuilder::get_etcd_client() {
    return etcd_client_;
}

/**
 * @brief 获取 brpc 服务器实例实现
 * 
 * @return brpc::Server* brpc 服务器指针
 */
brpc::Server* SpeechServerBuilder::get_brpc_server() {
    return brpc_server_.get();
}

/**
 * @brief 注册 brpc 服务实现实现
 * 
 * 将 brpc 服务实现对象添加到已注册服务列表中，在 build() 时统一注册到服务器。
 * 
 * @param service brpc 服务实现对象指针
 * @return SpeechServerBuilder& 返回自身引用，支持链式调用
 */
SpeechServerBuilder& SpeechServerBuilder::register_brpc_service(google::protobuf::Service* service) {
    registered_services_.push_back(service);
    LOG_INFO("[SpeechServerBuilder] BRPC service registered");
    return *this;
}

/**
 * @brief 向 etcd 注册服务实现
 * 
 * 将当前服务注册到 etcd 服务注册中心，供其他服务通过服务发现获取。
 * 优先使用外部访问地址进行注册（适用于云环境），如果未设置则使用监听地址。
 * 
 * @param service_name 服务名称，如 "speech_service"
 * @return bool 注册成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::register_service_to_etcd(const std::string& service_name) {
    // 检查 etcd 客户端是否已初始化
    if (!etcd_client_) {
        LOG_ERROR("[SpeechServerBuilder] Etcd client not initialized");
        return false;
    }

    // 确定注册地址：优先使用外部地址，否则使用监听地址
    std::string host_address;
    if (!config_.external_addr.empty() && config_.external_port > 0) {
        host_address = config_.external_addr + ":" + std::to_string(config_.external_port);
    } else {
        host_address = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    }

    // 调用 etcd 客户端注册服务
    bool result = etcd_client_->register_service(service_name, host_address);
    if (result) {
        LOG_INFO("[SpeechServerBuilder] Service registered to etcd: {} -> {}", service_name, host_address);
    } else {
        LOG_ERROR("[SpeechServerBuilder] Failed to register service to etcd");
    }

    return result;
}

/**
 * @brief 启动 brpc 服务器实现
 * 
 * 在指定地址和端口上启动 brpc 服务监听，设置空闲连接超时为 -1（不超时）。
 * 
 * @return bool 启动成功返回 true，失败返回 false
 */
bool SpeechServerBuilder::start() {
    // 检查 brpc 服务器是否已初始化
    if (!brpc_server_) {
        LOG_ERROR("[SpeechServerBuilder] BRPC server not initialized");
        return false;
    }

    // 拼接监听地址（格式：ip:port）
    std::string addr = config_.listen_addr + ":" + std::to_string(config_.listen_port);
    
    // 配置服务器选项
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;  // 禁用空闲连接超时

    // 启动服务器
    if (brpc_server_->Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[SpeechServerBuilder] Failed to start BRPC server on {}", addr);
        return false;
    }

    // 设置运行状态为 true
    running_ = true;
    LOG_INFO("[SpeechServerBuilder] BRPC server started on {}", addr);
    return true;
}

/**
 * @brief 停止服务器实现
 * 
 * 按顺序释放资源：
 * 1. 设置运行状态为 false
 * 2. 停止 etcd 保活线程（释放 Lease）
 * 3. 停止 brpc 服务器（停止监听并等待线程退出）
 */
void SpeechServerBuilder::stop() {
    running_ = false;

    // 停止 etcd 保活，释放 etcd 连接
    if (etcd_client_) {
        etcd_client_->stop();
        LOG_INFO("[SpeechServerBuilder] Etcd client stopped");
    }

    // 停止 brpc 服务器
    if (brpc_server_) {
        brpc_server_->Stop(0);  // 立即停止
        brpc_server_->Join();   // 等待线程退出
        LOG_INFO("[SpeechServerBuilder] BRPC server stopped");
    }
}

}
