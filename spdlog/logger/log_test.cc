#include "log.hpp"
#include <gflags/gflags.h>

DEFINE_bool(debug, true, "是否调试模式：true输出到控制台，false输出到文件");
DEFINE_string(log_file, "app.log", "发布模式下的日志文件名");
DEFINE_string(log_level, "info", "日志输出级别：trace/debug/info/warn/error/critical");

mylog::LogLevel parse_log_level(const std::string& level_str) {
    if (level_str == "trace") return mylog::LogLevel::TRACE;
    if (level_str == "debug") return mylog::LogLevel::DEBUG;
    if (level_str == "info") return mylog::LogLevel::INFO;
    if (level_str == "warn") return mylog::LogLevel::WARN;
    if (level_str == "error") return mylog::LogLevel::ERROR;
    if (level_str == "critical") return mylog::LogLevel::CRITICAL;
    return mylog::LogLevel::INFO;
}

void test_log() {
    LOG_TRACE("这是一条trace日志，用于追踪程序执行流程");
    LOG_DEBUG("这是一条debug日志，开发阶段使用");
    LOG_INFO("这是一条info日志，一般信息");
    LOG_WARN("这是一条warn日志，可能存在问题");
    LOG_ERROR("这是一条error日志，功能异常");
    LOG_CRITICAL("这是一条critical日志，严重错误");
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    mylog::init(
        FLAGS_debug,
        FLAGS_log_file,
        parse_log_level(FLAGS_log_level)
    );

    LOG_INFO("=== 日志测试开始 ===");
    LOG_INFO("调试模式: {}", FLAGS_debug ? "true" : "false");
    LOG_INFO("日志文件: {}", FLAGS_log_file);
    LOG_INFO("日志级别: {}", FLAGS_log_level);
    
    test_log();
    
    LOG_INFO("=== 日志测试结束 ===");
    return 0;
}
