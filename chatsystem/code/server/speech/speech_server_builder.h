#ifndef SPEECH_SERVER_BUILDER_H
#define SPEECH_SERVER_BUILDER_H

#include <string>
#include <memory>
#include <vector>
#include <atomic>

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "log.hpp"
#include "speech_client.hpp"
#include "etcd_client.hpp"

namespace speech {

/**
 * @brief 服务器配置结构体
 * 
 * 存储语音识别服务器所需的所有配置参数，包括网络监听、etcd注册、
 * 语音识别凭证和日志相关配置。
 */
struct ServerConfig {
    std::string listen_addr;      ///< 服务监听地址，默认 "0.0.0.0"
    int listen_port;              ///< 服务监听端口，默认 8080
    std::string etcd_addr;        ///< etcd 服务器地址，默认 "127.0.0.1"
    int etcd_port;                ///< etcd 服务器端口，默认 2379
    std::string external_addr;    ///< 外部访问地址（云服务器绑定地址与监听地址不一致时使用）
    int external_port;            ///< 外部访问端口
    std::string app_id;           ///< 百度语音识别 AppID
    std::string api_key;          ///< 百度语音识别 API Key
    std::string secret_key;       ///< 百度语音识别 Secret Key
    bool is_debug;                ///< 是否调试模式，调试模式下日志输出到控制台
    std::string log_file;         ///< 日志文件路径
    std::string log_level;        ///< 日志输出级别
};

/**
 * @brief 语音识别服务器建造者类
 * 
 * 采用建造者模式，统一管理 brpc 服务、语音识别客户端、etcd 客户端三者的构造过程。
 * 通过链式调用方式，使得服务器的初始化配置更加清晰、简洁。
 * 
 * 使用示例：
 * @code
 * speech::SpeechServerBuilder builder;
 * bool success = builder
 *     .with_listen_address("0.0.0.0")
 *     .with_listen_port(8888)
 *     .with_etcd_address("127.0.0.1")
 *     .with_speech_credentials(app_id, api_key, secret_key)
 *     .register_brpc_service(&service_impl)
 *     .build();
 * @endcode
 */
class SpeechServerBuilder {
public:
    /**
     * @brief 构造函数
     * 
     * 初始化默认配置参数，设置监听地址、端口、etcd地址等默认值。
     */
    SpeechServerBuilder();

    /**
     * @brief 析构函数
     * 
     * 自动调用 stop() 方法，释放所有资源。
     */
    ~SpeechServerBuilder();

    /**
     * @brief 设置服务监听地址
     * 
     * @param addr 监听地址，如 "0.0.0.0" 表示监听所有网络接口
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_listen_address(const std::string& addr);

    /**
     * @brief 设置服务监听端口
     * 
     * @param port 监听端口号
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_listen_port(int port);

    /**
     * @brief 设置外部访问地址（用于etcd注册）
     * 
     * 当云服务器绑定地址与监听地址不一致时，需要设置此参数。
     * 例如：服务器监听内网地址，但对外暴露公网地址。
     * 
     * @param addr 外部访问地址
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_external_address(const std::string& addr);

    /**
     * @brief 设置外部访问端口（用于etcd注册）
     * 
     * @param port 外部访问端口号
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_external_port(int port);

    /**
     * @brief 设置 etcd 服务器地址
     * 
     * @param addr etcd 服务器地址，如 "127.0.0.1"
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_etcd_address(const std::string& addr);

    /**
     * @brief 设置 etcd 服务器端口
     * 
     * @param port etcd 服务器端口，默认 2379
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_etcd_port(int port);

    /**
     * @brief 设置 etcd Lease 租约时长
     * 
     * Lease 用于服务注册的保活机制，租约过期后服务会自动从 etcd 删除。
     * 
     * @param ttl 租约时长，单位秒，默认 30 秒
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_etcd_lease_ttl(int ttl);

    /**
     * @brief 设置百度语音识别凭证
     * 
     * 需要在百度AI开放平台申请获取 AppID、API Key 和 Secret Key。
     * 
     * @param app_id 百度语音识别 AppID
     * @param api_key 百度语音识别 API Key
     * @param secret_key 百度语音识别 Secret Key
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_speech_credentials(const std::string& app_id, 
                                                  const std::string& api_key, 
                                                  const std::string& secret_key);

    /**
     * @brief 设置调试模式
     * 
     * 调试模式下日志输出到控制台，生产模式下日志输出到文件。
     * 
     * @param debug true 表示调试模式，false 表示生产模式
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_debug_mode(bool debug);

    /**
     * @brief 设置日志文件路径
     * 
     * @param file 日志文件路径，如 "speech_server.log"
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_log_file(const std::string& file);

    /**
     * @brief 设置日志输出级别
     * 
     * @param level 日志级别，可选值：TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& with_log_level(mylog::LogLevel level);

    /**
     * @brief 构建所有组件
     * 
     * 按顺序初始化日志模块、语音识别客户端、brpc服务器和etcd客户端。
     * 任何一步失败都会返回 false。
     * 
     * @return bool 构建成功返回 true，失败返回 false
     */
    bool build();

    /**
     * @brief 获取语音识别客户端
     * 
     * @return std::shared_ptr<SpeechClient> 语音识别客户端智能指针
     */
    std::shared_ptr<SpeechClient> get_speech_client() const;

    /**
     * @brief 获取 etcd 注册客户端
     * 
     * @return std::unique_ptr<etcd::ServiceRegisterClient>& etcd 注册客户端引用
     */
    std::unique_ptr<etcd::ServiceRegisterClient>& get_etcd_client();

    /**
     * @brief 获取 brpc 服务器实例
     * 
     * @return brpc::Server* brpc 服务器指针
     */
    brpc::Server* get_brpc_server();

    /**
     * @brief 注册 brpc 服务实现
     * 
     * 将业务服务实现注册到 brpc 服务器中，支持链式调用。
     * 
     * @param service brpc 服务实现对象指针
     * @return SpeechServerBuilder& 返回自身引用，支持链式调用
     */
    SpeechServerBuilder& register_brpc_service(google::protobuf::Service* service);

    /**
     * @brief 向 etcd 注册服务
     * 
     * 将当前服务注册到 etcd 服务注册中心，供其他服务发现和调用。
     * 
     * @param service_name 服务名称，如 "speech_service"
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
     * 停止 etcd 保活线程和 brpc 服务器，释放相关资源。
     */
    void stop();

private:
    /**
     * @brief 初始化日志模块
     * 
     * 根据配置初始化 spdlog 日志系统，设置日志输出方式和级别。
     * 
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_logger();

    /**
     * @brief 初始化语音识别客户端
     * 
     * 使用配置的凭证创建百度语音识别客户端实例。
     * 
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_speech_client();

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
     * 创建 etcd 服务注册客户端实例，用于服务注册和保活。
     * 
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool init_etcd_client();

    ServerConfig config_;                                  ///< 服务器配置参数
    int etcd_lease_ttl_;                                  ///< etcd Lease 租约时长（秒）
    mylog::LogLevel log_level_;                           ///< 日志输出级别

    std::shared_ptr<SpeechClient> speech_client_;         ///< 语音识别客户端
    std::unique_ptr<etcd::ServiceRegisterClient> etcd_client_;  ///< etcd 服务注册客户端
    std::unique_ptr<brpc::Server> brpc_server_;           ///< brpc 服务器实例
    std::atomic<bool> running_;                           ///< 运行状态标志

    std::vector<google::protobuf::Service*> registered_services_;  ///< 已注册的 brpc 服务列表
};

}

#endif
