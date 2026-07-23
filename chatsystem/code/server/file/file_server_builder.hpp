// =============================================================================
// file_server_builder.hpp - 文件服务器建造者类声明（建造者模式）
// =============================================================================
// 本头文件声明 FileServerBuilder 类，采用建造者模式统一管理文件子服务的
// 各组件构造过程，包括日志、文件存储、brpc 服务、etcd 客户端。
//
// 设计模式：建造者模式（Builder Pattern）
//   - 通过链式调用（Fluent API）配置服务器参数，代码清晰简洁
//   - 将复杂对象的构造过程与表示分离，便于扩展配置项
//   - 同一份构造过程可以创建不同的表示（如不同存储后端）
//
// 使用示例：
//   file::FileServerBuilder builder;
//   bool success = builder
//       .with_listen_address("0.0.0.0")
//       .with_listen_port(10000)
//       .with_storage_dir("./file_storage")
//       .with_etcd_address("127.0.0.1")
//       .with_etcd_port(2379)
//       .register_brpc_service(&service_impl)
//       .build();
// =============================================================================

#ifndef FILE_SERVER_BUILDER_HPP
#define FILE_SERVER_BUILDER_HPP

#include <string>
#include <memory>
#include <vector>
#include <atomic>

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "log.hpp"
#include "file_storage.hpp"
#include "local_file_storage.hpp"
#include "etcd_client.hpp"

namespace file {

/**
 * @brief 文件服务器配置结构体
 *
 * 存储文件子服务运行所需的所有配置参数，包括网络监听、文件存储路径、
 * etcd 注册和日志相关配置。
 */
struct FileServerConfig {
    std::string listen_addr;      ///< 服务监听地址，默认 "0.0.0.0"
    int listen_port;              ///< 服务监听端口，默认 10000
    std::string storage_dir;      ///< 文件存储根目录路径，默认 "./file_storage"
    std::string etcd_addr;        ///< etcd 服务器地址，默认 "127.0.0.1"
    int etcd_port;                ///< etcd 服务器端口，默认 2379
    std::string external_addr;    ///< 外部访问地址（云环境公网地址，用于etcd注册）
    int external_port;            ///< 外部访问端口（用于etcd注册）
    bool is_debug;                ///< 是否调试模式（true 输出到控制台，false 输出到文件）
    std::string log_file;         ///< 日志文件路径
};

/**
 * @brief 文件服务器建造者类
 *
 * 采用建造者模式，统一管理 brpc 服务、文件存储、etcd 客户端三者的构造过程。
 * 通过链式调用方式配置所有参数，使得服务器的初始化过程清晰、简洁。
 *
 * 设计要点：
 *   1. 配置参数与构造逻辑分离：所有配置先收集到 ServerConfig，build() 时统一应用
 *   2. 资源管理采用智能指针，避免内存泄漏
 *   3. 析构函数自动调用 stop()，确保资源被正确清理
 *   4. 单一职责：每个 init_xxx() 方法只负责一类组件的初始化
 */
class FileServerBuilder {
public:
    /**
     * @brief 构造函数
     *
     * 初始化所有配置参数为默认值。
     */
    FileServerBuilder();

    /**
     * @brief 析构函数
     *
     * 自动调用 stop() 释放所有资源。
     */
    ~FileServerBuilder();

    // ==================== 配置方法（链式调用） ====================

    /**
     * @brief 设置服务监听地址
     * @param addr 监听地址，如 "0.0.0.0" 表示监听所有网络接口
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_listen_address(const std::string& addr);

    /**
     * @brief 设置服务监听端口
     * @param port 监听端口号
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_listen_port(int port);

    /**
     * @brief 设置文件存储根目录
     * @param dir 存储目录路径
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_storage_dir(const std::string& dir);

    /**
     * @brief 设置外部访问地址（用于 etcd 注册）
     *
     * 当服务器部署在云环境时，监听地址（内网）与外部访问地址（公网）可能不一致，
     * 此时需要设置外部访问地址用于 etcd 注册，便于其他服务通过公网访问。
     *
     * @param addr 外部访问地址
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_external_address(const std::string& addr);

    /**
     * @brief 设置外部访问端口（用于 etcd 注册）
     * @param port 外部访问端口号
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_external_port(int port);

    /**
     * @brief 设置 etcd 服务器地址
     * @param addr etcd 服务器地址，如 "127.0.0.1"
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_etcd_address(const std::string& addr);

    /**
     * @brief 设置 etcd 服务器端口
     * @param port etcd 服务器端口，默认 2379
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_etcd_port(int port);

    /**
     * @brief 设置 etcd Lease 租约时长
     *
     * Lease 用于服务注册的保活机制，租约过期后服务自动从 etcd 删除。
     *
     * @param ttl 租约时长（秒），默认 30 秒
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_etcd_lease_ttl(int ttl);

    /**
     * @brief 设置调试模式
     *
     * 调试模式下日志输出到控制台，生产模式下日志输出到文件。
     *
     * @param debug true 调试模式，false 生产模式
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_debug_mode(bool debug);

    /**
     * @brief 设置日志文件路径
     * @param file 日志文件路径
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_log_file(const std::string& file);

    /**
     * @brief 设置日志输出级别
     * @param level 日志级别枚举值
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& with_log_level(mylog::LogLevel level);

    // ==================== 服务注册与构建 ====================

    /**
     * @brief 注册 brpc 服务实现
     *
     * 将业务服务实现注册到 brpc 服务器中，支持链式调用。
     *
     * @param service brpc 服务实现对象指针
     * @return FileServerBuilder& 返回自身引用，支持链式调用
     */
    FileServerBuilder& register_brpc_service(google::protobuf::Service* service);

    /**
     * @brief 构建所有组件
     *
     * 按顺序初始化以下组件：
     *   1. 日志模块（必须最先初始化，后续组件依赖日志）
     *   2. 文件存储实例
     *   3. brpc 服务器（包含所有服务实现）
     *   4. etcd 客户端（用于服务注册）
     *
     * 任何一步失败都会立即返回 false。
     *
     * @return bool 构建成功返回 true，失败返回 false
     */
    bool build();

    // ==================== 访问器方法 ====================

    /**
     * @brief 获取文件存储实例
     * @return FileStoragePtr 文件存储智能指针
     */
    FileStoragePtr get_storage() const;

    /**
     * @brief 获取 etcd 注册客户端
     * @return std::unique_ptr<etcd::ServiceRegisterClient>& etcd 客户端引用
     */
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client();

    /**
     * @brief 获取 brpc 服务器实例
     * @return brpc::Server* brpc 服务器指针
     */
    brpc::Server* get_brpc_server();

    // ==================== 运行时方法 ====================

    /**
     * @brief 向 etcd 注册服务
     *
     * 将当前服务注册到 etcd 服务注册中心，供其他服务通过服务发现获取。
     *
     * @param service_name 服务名称，如 "file_service"
     * @return bool 注册成功返回 true，失败返回 false
     */
    bool register_service_to_etcd(const std::string& service_name);

    /**
     * @brief 启动 brpc 服务器
     *
     * 在指定地址和端口上启动 brpc 服务监听。
     *
     * @return bool 启动成功返回 true，失败返回 false
     */
    bool start();

    /**
     * @brief 停止服务器，释放所有资源
     *
     * 按顺序停止：
     *   1. etcd 保活线程
     *   2. brpc 服务器（停止监听并等待线程退出）
     */
    void stop();

private:
    // ==================== 私有初始化方法 ====================

    /**
     * @brief 初始化日志模块
     *
     * 根据配置初始化 spdlog 日志系统。
     *
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_logger();

    /**
     * @brief 初始化文件存储实例
     *
     * 创建 LocalFileStorage 实例，使用配置的存储目录。
     *
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_storage();

    /**
     * @brief 初始化 brpc 服务器
     *
     * 创建 brpc 服务器实例并注册所有已添加的服务实现。
     *
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_brpc_server();

    /**
     * @brief 初始化 etcd 客户端
     *
     * 创建 etcd 服务注册客户端实例。
     *
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_etcd_client();

    // ==================== 成员变量 ====================

    FileServerConfig config_;                              ///< 服务器配置参数
    int etcd_lease_ttl_;                                  ///< etcd Lease 租约时长（秒）
    mylog::LogLevel log_level_;                           ///< 日志输出级别

    FileStoragePtr storage_;                              ///< 文件存储实例
    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;  ///< etcd 服务注册客户端
    std::unique_ptr<brpc::Server> brpc_server_;           ///< brpc 服务器实例
    std::atomic<bool> running_;                           ///< 运行状态标志

    std::vector<google::protobuf::Service*> registered_services_;  ///< 已注册的 brpc 服务列表
};

} // namespace file

#endif // FILE_SERVER_BUILDER_HPP
