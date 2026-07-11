#include <spdlog/spdlog.h>
#include <spdlog/async.h>                          // 异步日志需要此头文件
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <iostream>
#include <thread>

int main()
{
    // 初始化异步线程池：队列大小 8192，1 个工作线程
    spdlog::init_thread_pool(8192, 1);

    // 方式一：创建异步文件日志（spdlog 1.12 推荐）
    auto file_logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
        "file_logger",                           // logger 名称
        "async.log",                             // 文件名
        true                                     // truncate = true，每次运行清空
    );
    file_logger->set_level(spdlog::level::trace);
    file_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%-8l] %v");

    // 方式二：创建异步控制台彩色日志
    auto console_logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>(
        "console_logger"
    );
    console_logger->set_level(spdlog::level::info);
    console_logger->set_pattern("[%n] [%-8l] %v");

    // 模拟多线程写入
    spdlog::info("=== 异步日志示例开始 ===");

    // 启动多个线程并发写日志
    std::thread t1([&]() {
        for (int i = 0; i < 5; ++i) {
            file_logger->info("线程1: 第{}次写入", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 5; ++i) {
            file_logger->debug("线程2: 第{}次写入", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::thread t3([&]() {
        for (int i = 0; i < 5; ++i) {
            file_logger->warn("线程3: 第{}次写入", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // 主线程继续写
    for (int i = 0; i < 5; ++i) {
        file_logger->error("主线程: 第{}次写入", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    t1.join();
    t2.join();
    t3.join();

    spdlog::info("=== 异步日志示例结束 ===");

    // 必须调用，否则可能有日志未写入就程序退出了
    spdlog::shutdown();

    return 0;
}
