// =============================================================================
// file_server.cc - 文件子服务主入口程序
// =============================================================================
// 本文件是文件子服务的主入口，负责：
//   1. 解析命令行参数（使用 gflags）
//   2. 创建文件服务实现对象
//   3. 使用建造者模式构建服务器（链式调用配置所有参数）
//   4. 启动 brpc 服务器
//   5. 向 etcd 注册服务
//   6. 阻塞等待退出信号
//   7. 释放资源并退出
//
// 编译：通过 CMakeLists.txt 中的 file_server 目标编译
// 运行：./file_server --port=10000 --storage_dir=./file_storage --debug=true
// =============================================================================

#include <gflags/gflags.h>

#include "file_server_builder.hpp"
#include "file_service_impl.hpp"

// ==================== gflags 命令行参数定义 ====================

/**
 * @brief 服务监听端口
 *
 * 默认值：10000（避免与语音服务 8888 端口冲突）
 */
DEFINE_int32(port, 10000, "TCP port of file server");

/**
 * @brief 服务监听地址
 *
 * 默认值：0.0.0.0（监听所有网络接口）
 */
DEFINE_string(listen_addr, "0.0.0.0", "Server listen address");

/**
 * @brief 文件存储根目录路径
 *
 * 服务端上传的文件将存储在此目录下。
 * 默认值：./file_storage
 */
DEFINE_string(storage_dir, "./file_storage", "File storage directory");

/**
 * @brief 外部访问地址（用于 etcd 注册）
 *
 * 当云服务器绑定地址与监听地址不一致时使用，为空则使用监听地址注册。
 * 默认值：空字符串
 */
DEFINE_string(external_addr, "", "External access address for etcd registration");

/**
 * @brief 外部访问端口（用于 etcd 注册）
 *
 * 默认值：0（使用监听端口）
 */
DEFINE_int32(external_port, 0, "External access port for etcd registration");

/**
 * @brief etcd 服务器地址
 *
 * 默认值：127.0.0.1
 */
DEFINE_string(etcd_addr, "127.0.0.1", "Etcd server address");

/**
 * @brief etcd 服务器端口
 *
 * 默认值：2379
 */
DEFINE_int32(etcd_port, 2379, "Etcd server port");

/**
 * @brief etcd Lease 租约时长
 *
 * 租约过期后服务自动从 etcd 删除，实现自动故障剔除。
 * 默认值：30 秒
 */
DEFINE_int32(etcd_lease_ttl, 30, "Etcd lease TTL in seconds");

/**
 * @brief 调试模式开关
 *
 * true：日志输出到控制台
 * false：日志输出到文件
 * 默认值：false
 */
DEFINE_bool(debug, false, "Run in debug mode with console logging");

/**
 * @brief 日志文件路径
 *
 * 默认值：file_server.log
 */
DEFINE_string(log_file, "file_server.log", "Log file path");

/**
 * @brief 日志输出级别
 *
 * 可选值：TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL
 * 默认值：INFO
 */
DEFINE_string(log_level, "INFO", "Log level: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL");

// ==================== 辅助函数 ====================

/**
 * @brief 日志级别字符串转枚举值
 *
 * 将命令行参数传入的日志级别字符串转换为 mylog::LogLevel 枚举值。
 *
 * @param level 日志级别字符串
 * @return mylog::LogLevel 日志级别枚举值
 */
mylog::LogLevel parse_log_level(const std::string& level) {
    if (level == "TRACE") return mylog::LogLevel::TRACE;
    if (level == "DEBUG") return mylog::LogLevel::DEBUG;
    if (level == "INFO") return mylog::LogLevel::INFO;
    if (level == "WARN") return mylog::LogLevel::WARN;
    if (level == "ERROR") return mylog::LogLevel::ERROR;
    if (level == "CRITICAL") return mylog::LogLevel::CRITICAL;
    // 默认返回 INFO 级别
    return mylog::LogLevel::INFO;
}

// ==================== 主函数 ====================

/**
 * @brief 文件子服务主函数
 *
 * 服务启动流程：
 *   1. 解析命令行参数
 *   2. 创建文件服务实现对象
 *   3. 使用建造者模式构建服务器（链式调用）
 *      - 设置监听地址和端口
 *      - 设置文件存储目录
 *      - 设置 etcd 地址和端口
 *      - 设置日志配置
 *      - 注册 brpc 服务实现
 *   4. 构建所有组件（日志、存储、brpc服务器、etcd客户端）
 *   5. 设置存储实例到服务实现
 *   6. 启动 brpc 服务器
 *   7. 向 etcd 注册服务（服务名：file_service）
 *   8. 阻塞等待直到收到退出信号（Ctrl+C）
 *   9. 停止服务器，释放资源
 *
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 程序退出码，0 表示成功，-1 表示失败
 */
int main(int argc, char* argv[]) {
    // 设置程序用法信息
    gflags::SetUsageMessage("File Storage Server");
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 创建文件服务实现对象（此时存储实例尚未初始化）
    file::FileServiceImpl service_impl;

    // 使用建造者模式构建服务器
    // 通过链式调用方式配置所有参数，代码清晰简洁
    file::FileServerBuilder builder;
    bool success = builder
        // 设置监听地址
        .with_listen_address(FLAGS_listen_addr)
        // 设置监听端口
        .with_listen_port(FLAGS_port)
        // 设置文件存储目录
        .with_storage_dir(FLAGS_storage_dir)
        // 设置外部访问地址（用于云环境）
        .with_external_address(FLAGS_external_addr)
        // 设置外部访问端口
        .with_external_port(FLAGS_external_port)
        // 设置 etcd 服务器地址
        .with_etcd_address(FLAGS_etcd_addr)
        // 设置 etcd 服务器端口
        .with_etcd_port(FLAGS_etcd_port)
        // 设置 etcd Lease 租约时长
        .with_etcd_lease_ttl(FLAGS_etcd_lease_ttl)
        // 设置调试模式
        .with_debug_mode(FLAGS_debug)
        // 设置日志文件路径
        .with_log_file(FLAGS_log_file)
        // 设置日志级别
        .with_log_level(parse_log_level(FLAGS_log_level))
        // 注册 brpc 服务实现
        .register_brpc_service(&service_impl)
        // 构建所有组件（日志、存储、brpc服务器、etcd客户端）
        .build();

    // 检查构建是否成功
    if (!success) {
        std::cerr << "Failed to build file server" << std::endl;
        return -1;
    }

    // 将构建完成的文件存储实例设置到服务实现对象中
    service_impl.set_storage(builder.get_storage());

    // 启动 brpc 服务器
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start file server" << std::endl;
        return -1;
    }

    // 向 etcd 注册服务（服务名称：file_service）
    success = builder.register_service_to_etcd("file_service");
    if (!success) {
        std::cerr << "Failed to register service to etcd" << std::endl;
        // 注意：etcd 注册失败不影响服务启动，仅记录警告
    }

    // 阻塞等待直到收到退出信号（Ctrl+C）
    builder.get_brpc_server()->RunUntilAskedToQuit();

    // 停止服务器，释放所有资源
    builder.stop();
    return 0;
}
