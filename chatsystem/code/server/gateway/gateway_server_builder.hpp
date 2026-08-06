// =============================================================================
// gateway_server_builder.hpp - 网关服务器建造者类声明（建造者模式）
// =============================================================================
// 本头文件声明 GatewayServerBuilder 类，采用建造者模式统一管理网关服务器的
// 各组件构造过程，包括日志、Redis客户端、RPC信道池、HTTP服务器、WebSocket服务器。
//
// 设计模式：建造者模式（Builder Pattern）
//   - 通过链式调用（Fluent API）配置服务器参数，代码清晰简洁
//   - 将复杂对象的构造过程与表示分离，便于扩展配置项
//
// 使用示例：
//   GatewayServerBuilder builder;
//   builder.with_http_port(8888)
//          .with_ws_port(8889)
//          .with_redis_host("127.0.0.1")
//          .with_etcd_address("127.0.0.1", 2379)
//          .build();
//   builder.start();
// =============================================================================

#ifndef GATEWAY_SERVER_BUILDER_HPP
#define GATEWAY_SERVER_BUILDER_HPP

#include <string>
#include <memory>
#include <atomic>

#include <gflags/gflags.h>

#include "log.hpp"
#include "etcd_client.hpp"
#include "brpc_client.hpp"
#include "redis_client.hpp"
#include "gateway_server_impl.hpp"

namespace gateway {

/**
 * @brief 网关服务器配置结构体
 */
struct GatewayServerConfig {
    // HTTP 服务器配置
    std::string http_listen_addr;   ///< HTTP服务监听地址
    int http_port;                    ///< HTTP服务监听端口

    // WebSocket 服务器配置
    std::string ws_listen_addr;     ///< WebSocket服务监听地址
    int ws_port;                     ///< WebSocket服务端口

    // etcd 配置（用于服务发现）
    std::string etcd_addr;           ///< etcd服务器地址
    int etcd_port;                   ///< etcd服务器端口

    // Redis 配置（用于会话鉴权）
    std::string redis_host;          ///< Redis主机地址
    int redis_port;                  ///< Redis端口
    int redis_db;                    ///< Redis数据库编号

    // 日志配置
    bool is_debug;                   ///< 是否调试模式
    std::string log_file;            ///< 日志文件路径
};

/**
 * @brief 网关服务器建造者类
 *
 * 采用建造者模式，统一管理网关服务器的HTTP、WebSocket、Redis、RPC信道池等
 * 组件的构造过程。支持链式调用配置参数。
 */
class GatewayServerBuilder {
public:
    GatewayServerBuilder();
    ~GatewayServerBuilder();

    // ==================== 配置方法（链式调用） ====================

    /// 设置HTTP服务监听地址
    GatewayServerBuilder& with_http_listen_address(const std::string& addr);
    /// 设置HTTP服务端口
    GatewayServerBuilder& with_http_port(int port);

    /// 设置WebSocket服务监听地址
    GatewayServerBuilder& with_ws_listen_address(const std::string& addr);
    /// 设置WebSocket服务端口
    GatewayServerBuilder& with_ws_port(int port);

    /// 设置etcd服务器地址
    GatewayServerBuilder& with_etcd_address(const std::string& addr);
    /// 设置etcd服务器端口
    GatewayServerBuilder& with_etcd_port(int port);

    /// 设置Redis主机地址
    GatewayServerBuilder& with_redis_host(const std::string& host);
    /// 设置Redis端口
    GatewayServerBuilder& with_redis_port(int port);
    /// 设置Redis数据库编号
    GatewayServerBuilder& with_redis_db(int db);

    /// 设置调试模式（控制台彩色输出 vs 文件输出）
    GatewayServerBuilder& with_debug_mode(bool debug);
    /// 设置日志文件路径
    GatewayServerBuilder& with_log_file(const std::string& file);
    /// 设置日志级别
    GatewayServerBuilder& with_log_level(mylog::LogLevel level);

    // ==================== 构建与运行 ====================

    /**
     * @brief 构建所有组件
     *
     * 按顺序初始化：日志 -> Redis -> RPC信道池 -> 网关服务器
     */
    bool build();

    /**
     * @brief 启动网关服务器
     *
     * 启动HTTP和WebSocket服务器，开始接收请求。
     */
    bool start();

    /**
     * @brief 停止网关服务器
     *
     * 停止所有组件，释放资源。
     */
    void stop();

    // ==================== 访问器方法 ====================

    std::shared_ptr<redis_client::RedisClient> get_redis_client() const;
    std::shared_ptr<brpc::ServiceChannelPool> get_channel_pool() const;
    GatewayServerImpl* get_gateway_server();

private:
    // ==================== 私有初始化方法 ====================

    /// 初始化日志系统
    bool init_logger();
    /// 初始化Redis客户端
    bool init_redis();
    /// 初始化RPC信道池（基于etcd服务发现）
    bool init_channel_pool();
    /// 初始化网关服务器（HTTP + WebSocket）
    bool init_gateway_server();

    GatewayServerConfig config_;                                       ///< 服务器配置
    mylog::LogLevel log_level_;                                        ///< 日志级别
    std::atomic<bool> running_;                                       ///< 运行状态标志

    std::shared_ptr<redis_client::RedisClient> redis_client_;         ///< Redis客户端
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;          ///< RPC信道池
    GatewayServerImpl gateway_server_;                                ///< 网关服务器实现
};

} // namespace gateway

#endif // GATEWAY_SERVER_BUILDER_HPP
