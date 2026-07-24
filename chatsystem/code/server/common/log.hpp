// =============================================================================
// log.hpp - 日志模块封装
// =============================================================================
// 基于 spdlog 封装的轻量级日志系统，提供统一的日志输出接口。
//
// 设计要点：
//   - header-only 实现，无需单独编译
//   - 支持调试模式（彩色控制台输出）和发布模式（文件输出）
//   - 线程安全：全局 logger 为单例，初始化用互斥锁保护
//   - 通过宏自动注入 __FILE__ 和 __LINE__
//
// 使用示例：
//   mylog::init(true, "app.log", mylog::LogLevel::DEBUG);  // 调试模式
//   LOG_INFO("User {} logged in", user_id);
//   LOG_ERROR("Failed to connect: {}", e.what());
//
// 依赖：spdlog, fmt
// =============================================================================

#ifndef LOG_HPP
#define LOG_HPP

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace mylog {

/// 日志级别枚举，与 spdlog 的 level 一一对应

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

/// 内部实现细节，不对外暴露
namespace detail {

/// 获取全局 logger 单例（延迟初始化）
inline std::shared_ptr<spdlog::logger>& get_logger() {
    static std::shared_ptr<spdlog::logger> logger = nullptr;
    return logger;
}

/// 获取初始化互斥锁，保证 init() 线程安全
inline std::mutex& get_mutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace detail

/**
 * @brief 初始化日志系统（全局仅需调用一次）
 *
 * @param is_debug true=彩色控制台输出，false=文件输出
 * @param log_file 日志文件路径（非调试模式时使用）
 * @param level    最低输出级别，低于此级别的日志将被过滤
 */
inline void init(bool is_debug, const std::string& log_file = "app.log", LogLevel level = LogLevel::INFO) {
    std::lock_guard<std::mutex> lock(detail::get_mutex());
    
    if (detail::get_logger()) {
        return;
    }

    spdlog::level::level_enum spd_level;
    switch (level) {
        case LogLevel::TRACE:    spd_level = spdlog::level::trace;    break;
        case LogLevel::DEBUG:    spd_level = spdlog::level::debug;    break;
        case LogLevel::INFO:     spd_level = spdlog::level::info;     break;
        case LogLevel::WARN:     spd_level = spdlog::level::warn;     break;
        case LogLevel::ERROR:    spd_level = spdlog::level::err;      break;
        case LogLevel::CRITICAL: spd_level = spdlog::level::critical; break;
        default:                 spd_level = spdlog::level::info;     break;
    }

    if (is_debug) {
        detail::get_logger() = spdlog::stdout_color_mt("console");
        detail::get_logger()->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-8l] %v");
    } else {
        detail::get_logger() = spdlog::basic_logger_mt("file", log_file);
        detail::get_logger()->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-8l] %v");
    }
    
    detail::get_logger()->set_level(spd_level);
}

/**
 * @brief 底层日志输出函数（由宏调用，不建议直接使用）
 *
 * @param file  源文件名（由 LOG_xxx 宏自动注入）
 * @param line  行号（由 LOG_xxx 宏自动注入）
 * @param level 日志级别
 * @param format fmt 格式化字符串，如 "user_id: {}, name: {}"
 * @param args  可变参数
 */
template<typename... Args>
inline void log(const char* file, int line, LogLevel level, const char* format, Args&&... args) {
    auto& logger = detail::get_logger();
    if (!logger) {
        return;
    }

    switch (level) {
        case LogLevel::TRACE:    logger->trace("[{}:{}] {}", file, line, fmt::format(format, std::forward<Args>(args)...));    break;
        case LogLevel::DEBUG:    logger->debug("[{}:{}] {}", file, line, fmt::format(format, std::forward<Args>(args)...));    break;
        case LogLevel::INFO:     logger->info("[{}:{}] {}", file, line, fmt::format(format, std::forward<Args>(args)...));     break;
        case LogLevel::WARN:     logger->warn("[{}:{}] {}", file, line, fmt::format(format, std::forward<Args>(args)...));     break;
        case LogLevel::ERROR:    logger->error("[{}:{}] {}", file, line, fmt::format(format, std::forward<Args>(args)...));    break;
        case LogLevel::CRITICAL: logger->critical("[{}:{}] {}", file, line, fmt::format(format, std::forward<Args>(args)...)); break;
    }
}

// ==================== 日志宏定义 ====================
// 自动注入 __FILE__ 和 __LINE__，调用者无需手动传参
// 用法：LOG_INFO("message: {}", variable);

#define LOG_TRACE(...)   ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...)   ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)    ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(...)    ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(...)   ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::ERROR, __VA_ARGS__)
#define LOG_CRITICAL(...) ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::CRITICAL, __VA_ARGS__)

} // namespace mylog

#endif // LOG_HPP
