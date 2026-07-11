#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>   // stdout_color_mt 需要此头文件
#include <spdlog/sinks/basic_file_sink.h>       // basic_logger_mt 需要此头文件
#include <iostream>
#include <chrono>                               // flush_every 需要 chrono

int main()
{
    //设置刷新策略
    spdlog::flush_every(std::chrono::seconds(1)); // 每 1 秒定时刷新
    spdlog::flush_on(spdlog::level::debug);        // debug 及以上级别立即刷新
    //创建logger，默认日志级别为 info，需手动设为 trace 才能输出所有级别
    // auto logger = spdlog::stdout_color_mt("console");
    auto logger = spdlog::basic_logger_mt("console", "test.log");
    logger->set_level(spdlog::level::trace); // 设为 trace，输出所有级别日志
    logger->set_pattern("[%Y:%m:%d %H:%M:%S] [%-8l] %v");
    logger->info("Hello world!");
    logger->debug("Debug message");
    logger->error("Error message");
    logger->warn("Warn message");
    logger->trace("Trace message");
    
    return 0;
}
