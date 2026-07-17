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

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

namespace detail {

inline std::shared_ptr<spdlog::logger>& get_logger() {
    static std::shared_ptr<spdlog::logger> logger = nullptr;
    return logger;
}

inline std::mutex& get_mutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace detail

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

#define LOG_TRACE(...)   ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...)   ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)    ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(...)    ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(...)   ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::ERROR, __VA_ARGS__)
#define LOG_CRITICAL(...) ::mylog::log(__FILE__, __LINE__, ::mylog::LogLevel::CRITICAL, __VA_ARGS__)

} // namespace mylog

#endif // LOG_HPP
