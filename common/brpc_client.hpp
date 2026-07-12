#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

#include <brpc/channel.h>

#include "log.hpp"
#include "etcd_client.hpp"

/**
 * @brief bRPC 客户端二次封装
 * 
 * 封装 bRPC 框架的 Channel 管理，实现服务节点信道的统一管理和负载均衡。
 * 通过集成 etcd 服务发现，实现服务上下线时自动更新信道，减少模块耦合度。
 * 
 * 设计思想：
 * 1. ServiceChannelManager：管理单个服务的所有节点信道，采用 RR 轮询策略
 * 2. ServiceChannelPool：管理多个服务的信道管理器，集成 etcd 服务发现
 * 
 * 依赖说明：
 * - brpc: bRPC 框架（底层通信）
 * - etcd-cpp-apiv3: etcd 客户端库（服务发现）
 * - spdlog: 日志库（通过 log.hpp 封装）
 */
namespace brpc {

/**
 * @brief 单个服务的信道管理器
 * 
 * 负责管理某个服务的所有节点通信信道，支持添加/删除节点、RR 轮询获取信道。
 * 一个服务可能有多个节点提供服务，每个节点都有自己的 Channel。
 */
class ServiceChannelManager {
public:
    /**
     * @brief 信道信息结构体
     * 
     * 存储服务节点的主机地址和对应的 bRPC Channel 对象。
     */
    struct ChannelInfo {
        std::string host;                              ///< 服务节点主机地址，格式为 "IP:Port"
        std::shared_ptr<::brpc::Channel> channel;      ///< bRPC 客户端通信通道，智能指针管理生命周期
    };

    /**
     * @brief 构造函数
     * 
     * @param service_name 服务名称，用于标识和区分不同的服务
     */
    ServiceChannelManager(const std::string& service_name)
        : service_name_(service_name), round_robin_index_(0) {}

    /**
     * @brief 析构函数
     * 
     * 自动释放所有信道资源，由 shared_ptr 自动管理内存。
     */
    ~ServiceChannelManager() {}

    /**
     * @brief 添加服务节点信道
     * 
     * 创建一个新的 bRPC Channel 并初始化，连接到指定的服务节点。
     * 如果该主机的信道已存在，则直接返回成功。
     * 
     * @param host 服务节点主机地址，格式为 "IP:Port"，如 "192.168.1.100:8080"
     * @param options bRPC Channel 配置选项，包含协议、超时时间、重试次数等
     * @return 添加成功返回 true；初始化失败返回 false
     */
    bool add_channel(const std::string& host, const ::brpc::ChannelOptions& options = ::brpc::ChannelOptions()) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查该主机的信道是否已存在，避免重复创建
        for (const auto& info : channels_) {
            if (info.host == host) {
                LOG_WARN("[ServiceChannelManager] Channel already exists for host: {}", host);
                return true;
            }
        }

        // 创建 bRPC Channel 智能指针
        auto channel = std::make_shared<::brpc::Channel>();
        // 拷贝配置选项，避免修改原始参数
        ::brpc::ChannelOptions opts = options;
        // 设置默认超时时间为 1 秒
        if (opts.timeout_ms == 0) {
            opts.timeout_ms = 1000;
        }

        // 初始化 Channel，建立与服务端的连接
        if (channel->Init(host.c_str(), &opts) != 0) {
            LOG_ERROR("[ServiceChannelManager] Failed to init channel for host: {}", host);
            return false;
        }

        // 将信道信息添加到列表中
        channels_.push_back({host, channel});
        LOG_INFO("[ServiceChannelManager] Added channel for service: {}, host: {}", service_name_, host);
        return true;
    }

    /**
     * @brief 移除服务节点信道
     * 
     * 根据主机地址从信道列表中移除对应的信道，释放相关资源。
     * 
     * @param host 服务节点主机地址
     * @return 移除成功返回 true；未找到该主机的信道返回 false
     */
    bool remove_channel(const std::string& host) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = channels_.begin(); it != channels_.end(); ++it) {
            if (it->host == host) {
                channels_.erase(it);
                LOG_INFO("[ServiceChannelManager] Removed channel for service: {}, host: {}", service_name_, host);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 通过 RR 轮询获取一个信道
     * 
     * 使用 Round Robin 轮询策略，依次返回服务的各个节点信道，实现简单的负载均衡。
     * 
     * @return 返回一个可用的 bRPC Channel 智能指针；无可用信道时返回 nullptr
     */
    std::shared_ptr<::brpc::Channel> get_channel() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (channels_.empty()) {
            LOG_WARN("[ServiceChannelManager] No channels available for service: {}", service_name_);
            return nullptr;
        }

        // RR 轮询：原子递增索引，取模得到当前信道下标
        size_t index = round_robin_index_++ % channels_.size();
        return channels_[index].channel;
    }

    /**
     * @brief 获取指定主机的信道
     * 
     * 根据主机地址精确查找对应的信道。
     * 
     * @param host 服务节点主机地址
     * @return 返回对应的 bRPC Channel 智能指针；未找到返回 nullptr
     */
    std::shared_ptr<::brpc::Channel> get_channel(const std::string& host) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& info : channels_) {
            if (info.host == host) {
                return info.channel;
            }
        }
        return nullptr;
    }

    /**
     * @brief 获取信道数量
     * 
     * @return 当前服务的节点信道总数
     */
    size_t channel_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return channels_.size();
    }

    /**
     * @brief 检查是否有可用信道
     * 
     * @return 有可用信道返回 true；无可用信道返回 false
     */
    bool has_channels() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !channels_.empty();
    }

    /**
     * @brief 获取所有服务节点主机地址列表
     * 
     * @return 包含所有节点主机地址的向量
     */
    std::vector<std::string> get_hosts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> hosts;
        for (const auto& info : channels_) {
            hosts.push_back(info.host);
        }
        return hosts;
    }

private:
    std::string service_name_;               ///< 服务名称
    std::vector<ChannelInfo> channels_;      ///< 信道列表，存储服务的所有节点信道
    std::atomic<size_t> round_robin_index_;  ///< RR 轮询索引，原子操作保证线程安全
    mutable std::mutex mutex_;               ///< 互斥锁，保护 channels_ 的并发访问
};

/**
 * @brief 总体服务信道管理池
 * 
 * 管理多个服务的信道管理器，集成 etcd 服务发现，实现服务上下线时自动更新信道。
 * 提供统一的接口，用于获取指定服务的通信信道。
 */
class ServiceChannelPool {
public:
    /**
     * @brief 构造函数
     * 
     * 初始化空的服务信道管理池。
     */
    ServiceChannelPool() : etcd_discover_client_(nullptr), running_(false) {}

    /**
     * @brief 析构函数
     * 
     * 自动调用 stop() 停止服务，释放所有资源。
     */
    ~ServiceChannelPool() {
        stop();
    }

    /**
     * @brief 使用 etcd 初始化服务发现
     * 
     * 创建 etcd 服务发现客户端，设置服务上下线回调，启动 Watch 监听。
     * 同时会立即查询 etcd 中已注册的所有服务并创建对应的信道。
     * 
     * @param etcd_address etcd 服务器地址，格式为 "http://host:port"，如 "http://localhost:2379"
     * @param root_dir 服务注册的根目录，默认 "/services"
     * @return 初始化成功返回 true；失败返回 false
     */
    bool init_with_etcd(const std::string& etcd_address, const std::string& root_dir = "/services") {
        if (running_) {
            LOG_WARN("[ServiceChannelPool] Already running");
            return true;
        }

        // 创建 etcd 服务发现客户端
        etcd_discover_client_ = std::make_unique<etcd::ServiceDiscoverClient>(etcd_address);
        etcd_discover_client_->set_root_directory(root_dir);

        // 设置服务上线回调：当有新服务注册时，自动创建信道
        etcd_discover_client_->set_online_callback(
            [this](const std::string& service_name, const std::string& host_address) {
                handle_service_online(service_name, host_address);
            }
        );

        // 设置服务下线回调：当服务从 etcd 删除时，自动移除信道
        etcd_discover_client_->set_offline_callback(
            [this](const std::string& service_name, const std::string& host_address) {
                handle_service_offline(service_name, host_address);
            }
        );

        // 立即查询 etcd 中已注册的所有服务
        auto services = etcd_discover_client_->discover_services();
        for (const auto& pair : services) {
            add_service_channel(pair.first, pair.second);
        }

        // 启动 etcd Watch 监听服务变化
        etcd_discover_client_->start_watch();
        running_ = true;
        LOG_INFO("[ServiceChannelPool] Initialized with etcd: {}", etcd_address);
        return true;
    }

    /**
     * @brief 添加服务节点信道
     * 
     * 如果该服务的信道管理器不存在，则先创建；然后向管理器中添加节点信道。
     * 
     * @param service_name 服务名称
     * @param host_address 服务节点主机地址，格式为 "IP:Port"
     * @param options bRPC Channel 配置选项
     * @return 添加成功返回 true；失败返回 false
     */
    bool add_service_channel(const std::string& service_name, const std::string& host_address,
                            const ::brpc::ChannelOptions& options = ::brpc::ChannelOptions()) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 查找服务对应的信道管理器
        auto it = managers_.find(service_name);
        if (it == managers_.end()) {
            // 不存在则创建新的管理器
            auto manager = std::make_shared<ServiceChannelManager>(service_name);
            managers_[service_name] = manager;
            it = managers_.find(service_name);
        }
        // 向管理器中添加信道
        return it->second->add_channel(host_address, options);
    }

    /**
     * @brief 移除服务节点信道
     * 
     * 从服务的信道管理器中移除指定节点的信道，如果服务没有可用信道，则移除管理器。
     * 
     * @param service_name 服务名称
     * @param host_address 服务节点主机地址
     * @return 移除成功返回 true；未找到返回 false
     */
    bool remove_service_channel(const std::string& service_name, const std::string& host_address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = managers_.find(service_name);
        if (it == managers_.end()) {
            return false;
        }
        // 移除节点信道
        bool removed = it->second->remove_channel(host_address);
        // 如果服务没有可用信道，移除管理器
        if (it->second->channel_count() == 0) {
            managers_.erase(it);
        }
        return removed;
    }

    /**
     * @brief 获取指定服务的一个信道（RR 轮询）
     * 
     * 通过服务名称查找对应的信道管理器，然后使用 RR 轮询获取一个信道。
     * 
     * @param service_name 服务名称
     * @return 返回一个可用的 bRPC Channel 智能指针；服务不存在或无可用信道返回 nullptr
     */
    std::shared_ptr<::brpc::Channel> get_channel(const std::string& service_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = managers_.find(service_name);
        if (it == managers_.end()) {
            LOG_WARN("[ServiceChannelPool] No manager found for service: {}", service_name);
            return nullptr;
        }
        return it->second->get_channel();
    }

    /**
     * @brief 获取指定服务指定主机的信道
     * 
     * 根据服务名称和主机地址精确查找对应的信道。
     * 
     * @param service_name 服务名称
     * @param host_address 服务节点主机地址
     * @return 返回对应的 bRPC Channel 智能指针；未找到返回 nullptr
     */
    std::shared_ptr<::brpc::Channel> get_channel(const std::string& service_name, const std::string& host_address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = managers_.find(service_name);
        if (it == managers_.end()) {
            return nullptr;
        }
        return it->second->get_channel(host_address);
    }

    /**
     * @brief 获取指定服务的信道管理器
     * 
     * 返回服务对应的 ServiceChannelManager 对象，可用于更细粒度的信道管理。
     * 
     * @param service_name 服务名称
     * @return 返回 ServiceChannelManager 智能指针；服务不存在返回 nullptr
     */
    std::shared_ptr<ServiceChannelManager> get_manager(const std::string& service_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = managers_.find(service_name);
        if (it == managers_.end()) {
            return nullptr;
        }
        return it->second;
    }

    /**
     * @brief 检查服务是否可用
     * 
     * 判断指定服务是否存在且有可用信道。
     * 
     * @param service_name 服务名称
     * @return 服务可用返回 true；不可用返回 false
     */
    bool has_service(const std::string& service_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = managers_.find(service_name);
        return it != managers_.end() && it->second->has_channels();
    }

    /**
     * @brief 获取服务数量
     * 
     * @return 当前管理的服务总数
     */
    size_t service_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return managers_.size();
    }

    /**
     * @brief 停止服务，释放所有资源
     * 
     * 停止 etcd Watch 监听，关闭所有服务信道管理器。
     */
    void stop() {
        running_ = false;
        // 停止 etcd 服务发现
        if (etcd_discover_client_) {
            etcd_discover_client_->stop();
        }
        // 清空所有信道管理器
        std::lock_guard<std::mutex> lock(mutex_);
        managers_.clear();
    }

private:
    /**
     * @brief 处理服务上线事件
     * 
     * 当 etcd 监听到服务上线时，自动为该服务添加信道。
     * 
     * @param service_name 服务名称
     * @param host_address 服务节点主机地址
     */
    void handle_service_online(const std::string& service_name, const std::string& host_address) {
        LOG_INFO("[ServiceChannelPool] Service online: {} -> {}", service_name, host_address);
        add_service_channel(service_name, host_address);
    }

    /**
     * @brief 处理服务下线事件
     * 
     * 当 etcd 监听到服务下线时，自动移除该服务的信道。
     * 
     * @param service_name 服务名称
     * @param host_address 服务节点主机地址
     */
    void handle_service_offline(const std::string& service_name, const std::string& host_address) {
        LOG_INFO("[ServiceChannelPool] Service offline: {} -> {}", service_name, host_address);
        remove_service_channel(service_name, host_address);
    }

    std::map<std::string, std::shared_ptr<ServiceChannelManager>> managers_;  ///< 服务名称到信道管理器的映射
    std::unique_ptr<etcd::ServiceDiscoverClient> etcd_discover_client_;       ///< etcd 服务发现客户端
    std::atomic<bool> running_;                                               ///< 运行状态标志
    mutable std::mutex mutex_;                                                ///< 互斥锁，保护 managers_ 的并发访问
};

}
