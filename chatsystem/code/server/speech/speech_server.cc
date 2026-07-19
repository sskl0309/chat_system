// gflags: brpc 使用的命令行参数解析库
#include <gflags/gflags.h>

#include "speech_server_builder.h"
#include "speech_service_impl.h"

// ==================== gflags 命令行参数定义 ====================

/**
 * @brief 服务监听端口
 * 
 * 默认值：8888
 */
DEFINE_int32(port, 8888, "TCP port of speech server");

/**
 * @brief 服务监听地址
 * 
 * 默认值：0.0.0.0（监听所有网络接口）
 */
DEFINE_string(listen_addr, "0.0.0.0", "Server listen address");

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
 * @brief 百度语音识别 AppID
 * 
 * 需要在百度AI开放平台申请获取。
 */
DEFINE_string(app_id, "", "Baidu speech API AppID");

/**
 * @brief 百度语音识别 API Key
 * 
 * 需要在百度AI开放平台申请获取。
 */
DEFINE_string(api_key, "", "Baidu speech API Key");

/**
 * @brief 百度语音识别 Secret Key
 * 
 * 需要在百度AI开放平台申请获取。
 */
DEFINE_string(secret_key, "", "Baidu speech Secret Key");

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
 * 默认值：speech_server.log
 */
DEFINE_string(log_file, "speech_server.log", "Log file path");

/**
 * @brief 日志输出级别
 * 
 * 可选值：TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL
 * 默认值：INFO
 */
DEFINE_string(log_level, "INFO", "Log level: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL");

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

/**
 * @brief 语音识别服务主函数
 * 
 * 服务启动流程：
 * 1. 解析命令行参数
 * 2. 创建语音识别服务实现对象
 * 3. 使用建造者模式构建服务器（链式调用）
 *    - 设置监听地址和端口
 *    - 设置 etcd 地址和端口
 *    - 设置百度语音识别凭证
 *    - 设置日志配置
 *    - 注册 brpc 服务实现
 * 4. 构建所有组件（日志、语音客户端、brpc服务器、etcd客户端）
 * 5. 设置语音客户端到服务实现
 * 6. 启动 brpc 服务器
 * 7. 向 etcd 注册服务
 * 8. 阻塞等待直到收到退出信号
 * 9. 停止服务器，释放资源
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 程序退出码，0 表示成功，-1 表示失败
 */
int main(int argc, char* argv[]) {
    // 设置程序用法信息
    gflags::SetUsageMessage("Speech Recognition Server");
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 创建语音识别服务实现对象（此时语音客户端尚未初始化）
    speech::SpeechServiceImpl service_impl;

    // 使用建造者模式构建服务器
    // 通过链式调用方式配置所有参数，代码清晰简洁
    speech::SpeechServerBuilder builder;
    bool success = builder
        // 设置监听地址
        .with_listen_address(FLAGS_listen_addr)
        // 设置监听端口
        .with_listen_port(FLAGS_port)
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
        // 设置百度语音识别凭证
        .with_speech_credentials(FLAGS_app_id, FLAGS_api_key, FLAGS_secret_key)
        // 设置调试模式
        .with_debug_mode(FLAGS_debug)
        // 设置日志文件路径
        .with_log_file(FLAGS_log_file)
        // 设置日志级别
        .with_log_level(parse_log_level(FLAGS_log_level))
        // 注册 brpc 服务实现
        .register_brpc_service(&service_impl)
        // 构建所有组件（日志、语音客户端、brpc服务器、etcd客户端）
        .build();

    // 检查构建是否成功
    if (!success) {
        std::cerr << "Failed to build speech server" << std::endl;
        return -1;
    }

    // 将构建完成的语音客户端设置到服务实现对象中
    service_impl.set_speech_client(builder.get_speech_client());

    // 启动 brpc 服务器
    success = builder.start();
    if (!success) {
        std::cerr << "Failed to start speech server" << std::endl;
        return -1;
    }

    // 向 etcd 注册服务（服务名称：speech_service）
    success = builder.register_service_to_etcd("speech_service");
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
