#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>

#include "../common/log.hpp"

#include <etcd/SyncClient.hpp>
#include <etcd/Watcher.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Response.hpp>

/**
 * @brief etcd 服务注册与发现二次封装
 * 
 * 封装 etcd-cpp-client API，实现两种类型的客户端：
 * 1. ServiceRegisterClient：服务注册客户端，向 etcd 注册服务并通过 Lease 机制保活
 * 2. ServiceDiscoverClient：服务发现客户端，查找服务信息并监控服务上下线事件
 * 
 * 设计思想：
 * - Header-only 封装，无需单独编译链接，直接 include 即可使用
 * - 减少模块耦合度，将 etcd 作为键值存储系统进行封装，对外提供简洁接口
 * - 服务注册路径格式：/services/{service_name}/{host_address}
 * 
 * 依赖说明：
 * - etcd-cpp-apiv3: C++ etcd 客户端库（v3 API）
 * - gRPC: 底层通信框架
 * - cpprestsdk: HTTP 客户端支持
 */
namespace etcd {

/**
 * @brief 服务注册客户端类
 * 
 * 负责向 etcd 注册服务信息，并通过 Lease keepalive 机制保持服务活跃状态。
 * 当服务停止或连接断开时，Lease 过期后服务会自动从 etcd 中删除，实现自动故障剔除。
 * 
 * 使用示例：
 * @code
 * etcd::ServiceRegisterClient client("http://localhost:2379", 30);
 * client.register_service("user_service", "192.168.1.100:8080");
 * @endcode
 */
class ServiceRegisterClient {
public:
    /**
     * @brief 构造函数
     * 
     * @param etcd_address etcd 服务器地址，格式为 "http://host:port"，如 "http://localhost:2379"
     * @param lease_ttl Lease 租约时长，单位秒，默认30秒。建议设置为客户端健康检查周期的3倍以上
     */
    ServiceRegisterClient(const std::string &etcd_address, int lease_ttl = 30)
        : etcd_address_(etcd_address), lease_ttl_(lease_ttl), running_(false) {}
    
    /**
     * @brief 析构函数
     * 
     * 自动调用 stop() 停止服务注册，释放 etcd 连接和 Lease 资源。
     */
    ~ServiceRegisterClient() {
        stop();
    }
    
    /**
     * @brief 注册服务并启动保活机制
     * 
     * 创建 Lease 租约，将服务信息以键值对形式写入 etcd，路径格式为：
     *   Key:   /services/{service_name}/{host_address}
     *   Value: {host_address}
     * 同时启动 keepalive 定时续约线程，定期向 etcd 发送心跳，确保服务在 etcd 中保持活跃状态。
     * 当服务进程退出或网络断开时，Lease 续约失败，租约过期后服务自动从 etcd 删除。
     * 
     * @param service_name 服务名称，用于服务分类和查找，如 "user_service"、"order_service"
     * @param host_address 服务主机地址，格式为 "IP:Port"，如 "192.168.1.100:8080"
     * @return 注册成功返回 true；注册失败（网络错误、etcd 不可用、Lease 创建失败等）返回 false
     */
    bool register_service(const std::string &service_name, const std::string &host_address) {
        try {
            // 构建 etcd key，格式：/services/{service_name}/{host_address}
            std::string key = "/services/" + service_name + "/" + host_address;
            std::string value = host_address;
            
            // 创建 etcd 同步客户端
            client_ = std::make_unique<SyncClient>(etcd_address_);
            // 创建 Lease 并启动自动续约，返回 KeepAlive 对象用于管理续约
            keepalive_ = client_->leasekeepalive(lease_ttl_);
            
            // 检查 Lease 是否创建成功
            if (!keepalive_) {
                LOG_ERROR("[ServiceRegisterClient] Failed to create lease keepalive");
                return false;
            }
            
            // 获取 Lease ID，用于绑定到 key
            int64_t lease_id = keepalive_->Lease();
            // 将服务信息写入 etcd，并绑定 Lease
            etcd::Response resp = client_->put(key, value, lease_id);
            
            if (resp.is_ok()) {
                service_key_ = key;
                running_ = true;
                LOG_INFO("[ServiceRegisterClient] Registered service: {} -> {}", service_name, host_address);
                return true;
            } else {
                LOG_ERROR("[ServiceRegisterClient] Failed to put key: {}", resp.error_message());
            }
            
            return false;
        } catch (const std::exception &e) {
            LOG_ERROR("[ServiceRegisterClient] Exception: {}", e.what());
            return false;
        }
    }
    
    /**
     * @brief 停止服务注册，释放资源
     * 
     * 停止 keepalive 续约线程，释放 etcd 客户端连接。
     * 注意：调用此方法后，服务不会立即从 etcd 删除，而是等待 Lease 过期后自动删除。
     * 如果需要立即删除服务，可在调用 stop() 前手动删除 key。
     */
    void stop() {
        running_ = false;
        // 先释放 KeepAlive，停止续约
        keepalive_.reset();
        // 再释放客户端
        client_.reset();
    }
    
private:
    std::string etcd_address_;      ///< etcd 服务器地址
    int lease_ttl_;                 ///< Lease 租约时长（秒）
    std::string service_key_;       ///< 注册的服务键路径，如 "/services/user_service/192.168.1.100:8080"
    std::atomic<bool> running_;     ///< 运行状态标志，用于标记服务是否正在注册
    std::unique_ptr<SyncClient> client_;     ///< etcd 同步客户端，用于发送同步请求
    std::shared_ptr<KeepAlive> keepalive_;   ///< Lease 保活对象，内部维护续约线程
};

/**
 * @brief 服务发现客户端类
 * 
 * 负责从 etcd 查找服务信息，并通过 Watch 机制实时监控服务的上下线事件。
 * 支持设置根目录、服务上线回调和服务下线回调，便于上层应用及时感知服务变化。
 * 
 * 使用示例：
 * @code
 * etcd::ServiceDiscoverClient client("http://localhost:2379");
 * client.set_root_directory("/services");
 * client.set_online_callback([](const std::string& name, const std::string& addr) {
 *     std::cout << "Service online: " << name << " -> " << addr << std::endl;
 * });
 * client.set_offline_callback([](const std::string& name, const std::string& addr) {
 *     std::cout << "Service offline: " << name << " -> " << addr << std::endl;
 * });
 * client.start_watch();
 * auto services = client.discover_services();
 * @endcode
 */
class ServiceDiscoverClient {
public:
    /**
     * @brief 服务上线回调函数类型
     * 
     * 当有新服务注册到 etcd 时触发此回调。
     * 
     * @param service_name 服务名称，如 "user_service"
     * @param host_address 服务主机地址，格式为 "IP:Port"，如 "192.168.1.100:8080"
     */
    using ServiceOnlineCallback = std::function<void(const std::string &service_name, const std::string &host_address)>;
    
    /**
     * @brief 服务下线回调函数类型
     * 
     * 当服务从 etcd 删除（Lease 过期或手动删除）时触发此回调。
     * 
     * @param service_name 服务名称，如 "user_service"
     * @param host_address 服务主机地址，格式为 "IP:Port"，如 "192.168.1.100:8080"
     */
    using ServiceOfflineCallback = std::function<void(const std::string &service_name, const std::string &host_address)>;
    
    /**
     * @brief 构造函数
     * 
     * @param etcd_address etcd 服务器地址，格式为 "http://host:port"，如 "http://localhost:2379"
     */
    ServiceDiscoverClient(const std::string &etcd_address)
        : etcd_address_(etcd_address), watcher_(nullptr), running_(false) {}
    
    /**
     * @brief 析构函数
     * 
     * 自动调用 stop() 停止监控，释放 etcd 连接和 Watcher 资源。
     */
    ~ServiceDiscoverClient() {
        stop();
    }
    
    /**
     * @brief 设置服务上线回调函数
     * 
     * 当监控的根目录下有新的服务键被创建时，触发此回调函数。
     * 
     * @param callback 回调函数，接收两个参数：service_name（服务名称）和 host_address（主机地址）
     */
    void set_online_callback(ServiceOnlineCallback callback) {
        online_callback_ = std::move(callback);
    }
    
    /**
     * @brief 设置服务下线回调函数
     * 
     * 当监控的根目录下有服务键被删除时（包括 Lease 过期自动删除），触发此回调函数。
     * 
     * @param callback 回调函数，接收两个参数：service_name（服务名称）和 host_address（主机地址）
     */
    void set_offline_callback(ServiceOfflineCallback callback) {
        offline_callback_ = std::move(callback);
    }
    
    /**
     * @brief 设置监控的根目录
     * 
     * 服务发现将在指定的根目录下查找和监控服务。
     * 默认路径为空，实际查询时会使用 "/services" 作为默认根目录。
     * 服务存储格式为：{root_dir}/{service_name}/{host_address}
     * 
     * @param root_dir 根目录路径，如 "/services"、"/registry/services"
     */
    void set_root_directory(const std::string &root_dir) {
        root_dir_ = root_dir;
    }
    
    /**
     * @brief 发现所有服务
     * 
     * 从 etcd 中查询指定根目录下的所有服务，返回服务名称和主机地址的键值对列表。
     * 使用 range 查询方式，查询范围为 [root_dir, root_dir/~)，
     * 其中 "~" 是 ASCII 码中较大的字符，确保能匹配所有以 root_dir 为前缀的键。
     * 
     * @return 服务列表，每个元素为 std::pair<std::string, std::string>，
     *         第一个元素是服务名称（service_name），第二个元素是主机地址（host_address）
     */
    std::vector<std::pair<std::string, std::string>> discover_services() {
        std::vector<std::pair<std::string, std::string>> result;
        
        try {
            // 懒加载创建 etcd 客户端
            if (!client_) {
                client_ = std::make_unique<SyncClient>(etcd_address_);
            }
            
            // 构建 range 查询的结束键，使用 "~" 作为范围结束符
            std::string range_end = root_dir_;
            if (!range_end.empty() && range_end.back() != '/') {
                range_end += "/";
            }
            range_end += "~";
            
            // 执行范围查询，获取根目录下所有服务
            etcd::Response resp = client_->ls(root_dir_, range_end);
            
            if (resp.is_ok()) {
                // 遍历所有返回的键值对
                for (auto const& kv : resp.values()) {
                    std::string key = kv.key();
                    std::string value = kv.as_string();
                    
                    // 构建前缀用于匹配
                    std::string prefix = root_dir_;
                    if (!prefix.empty() && prefix.back() != '/') {
                        prefix += "/";
                    }
                    
                    // 验证 key 是否以指定前缀开头
                    if (key.substr(0, prefix.size()) == prefix) {
                        // 截取前缀后的部分，格式为 {service_name}/{host_address}
                        std::string rest = key.substr(prefix.size());
                        size_t slash_pos = rest.find("/");
                        if (slash_pos != std::string::npos) {
                            std::string service_name = rest.substr(0, slash_pos);
                            std::string host_address = rest.substr(slash_pos + 1);
                            result.emplace_back(service_name, host_address);
                        }
                    }
                }
            } else {
                LOG_ERROR("[ServiceDiscoverClient] ls failed: {}", resp.error_message());
            }
        } catch (const std::exception &e) {
            LOG_ERROR("[ServiceDiscoverClient] Exception in discover_services: {}", e.what());
        }
        
        return result;
    }
    
    /**
     * @brief 启动服务监控
     * 
     * 开启 Watch 监听指定根目录下的服务变化事件。
     * Watch 机制会持续监听 etcd 中指定范围的键变化，当有键被创建、修改或删除时，
     * etcd 会主动推送事件通知，客户端收到通知后触发相应的回调函数。
     * 
     * 注意：调用此方法前应先设置根目录（set_root_directory）和回调函数（set_online_callback/set_offline_callback）。
     * 重复调用此方法不会启动多个 Watcher。
     */
    void start_watch() {
        // 如果已经在运行，直接返回
        if (running_) {
            return;
        }
        
        try {
            // 懒加载创建 etcd 客户端
            if (!client_) {
                client_ = std::make_unique<SyncClient>(etcd_address_);
            }
            
            // 构建 Watch 的范围结束键
            std::string range_end = root_dir_;
            if (!range_end.empty() && range_end.back() != '/') {
                range_end += "/";
            }
            range_end += "~";
            
            running_ = true;
            
            // 创建 Watcher，监听指定范围的键变化
            // Watcher 内部会启动一个线程持续监听 etcd 的 Watch 事件
            watcher_.reset(new Watcher(*client_, root_dir_, range_end,
                [this](etcd::Response resp) {
                    // 当收到 Watch 事件时，调用回调处理函数
                    handle_watch_response(resp);
                }));
                
            LOG_INFO("[ServiceDiscoverClient] Watch started on: {}", root_dir_);
        } catch (const std::exception &e) {
            running_ = false;
            LOG_ERROR("[ServiceDiscoverClient] Exception in start_watch: {}", e.what());
        }
    }
    
    /**
     * @brief 停止服务监控，释放资源
     * 
     * 停止 Watcher 监听，释放 etcd 客户端连接。
     * 调用此方法后，不再接收服务上下线事件通知。
     */
    void stop() {
        running_ = false;
        // 先释放 Watcher，停止监听
        watcher_.reset();
        // 再释放客户端
        client_.reset();
    }
    
private:
    /**
     * @brief 处理 Watch 响应事件
     * 
     * 解析 etcd Watch 响应中的事件列表，根据事件类型（PUT 或 DELETE）触发相应的回调函数。
     * 对于 DELETE 事件，触发服务下线回调；对于其他事件（PUT），触发服务上线回调。
     * 
     * @param resp etcd Watch 响应对象，包含事件列表和响应状态
     */
    void handle_watch_response(etcd::Response &resp) {
        // 检查响应是否成功
        if (!resp.is_ok()) {
            return;
        }
        
        // 遍历所有事件
        for (auto const& event : resp.events()) {
            // 检查事件是否包含键值对数据
            if (!event.has_kv()) {
                continue;
            }
            
            // 获取事件中的键值对
            const etcd::Value& kv = event.kv();
            std::string key = kv.key();
            std::string value = kv.as_string();
            
            // 构建前缀用于匹配
            std::string prefix = root_dir_;
            if (!prefix.empty() && prefix.back() != '/') {
                prefix += "/";
            }
            
            // 验证 key 是否以指定前缀开头
            if (key.substr(0, prefix.size()) != prefix) {
                continue;
            }
            
            // 截取前缀后的部分，格式为 {service_name}/{host_address}
            std::string rest = key.substr(prefix.size());
            size_t slash_pos = rest.find("/");
            if (slash_pos == std::string::npos) {
                continue;
            }
            
            std::string service_name = rest.substr(0, slash_pos);
            std::string host_address = rest.substr(slash_pos + 1);
            
            // 根据事件类型触发相应的回调
            if (event.event_type() == etcd::Event::EventType::DELETE_) {
                // DELETE 事件：服务下线
                if (offline_callback_) {
                    offline_callback_(service_name, host_address);
                }
            } else {
                // PUT 事件：服务上线或更新
                if (online_callback_) {
                    online_callback_(service_name, host_address);
                }
            }
        }
    }
    
    std::string etcd_address_;              ///< etcd 服务器地址
    std::string root_dir_;                  ///< 服务根目录路径，默认 "/services"
    ServiceOnlineCallback online_callback_; ///< 服务上线回调函数
    ServiceOfflineCallback offline_callback_;///< 服务下线回调函数
    std::atomic<bool> running_;             ///< 运行状态标志，用于标记是否正在监控
    std::unique_ptr<SyncClient> client_;    ///< etcd 同步客户端
    std::unique_ptr<Watcher> watcher_;      ///< Watch 监控对象，内部维护监听线程
};

}