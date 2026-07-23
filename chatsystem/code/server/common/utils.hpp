// =============================================================================
// utils.hpp - 通用工具集
// =============================================================================
// 本头文件提供项目通用的工具函数，目前包含：
//   1. UUID 生成器（uuidGenerator）        - 用于生成全局唯一的字符串ID
//   2. 文件唯一ID生成器（FileIdGenerator） - 单例模式，专门用于生成文件子服务的唯一文件ID
//
// 设计要点：
//   - 采用 header-only 方式实现，无需单独编译，直接 #include 即可使用
//   - UUID 采用 "时间戳 + 随机数 + 自增计数器" 三重保证唯一性策略
//   - 单例模式确保进程内 ID 生成器唯一，避免多线程环境下的 ID 冲突
//
// 唯一性保证策略：
//   1. 时间戳（毫秒级）            - 区分不同时间点的请求
//   2. 随机数（16 位十六进制）     - 同一毫秒内的随机化
//   3. 原子自增计数器              - 同一进程内的严格递增，保证唯一性
//   4. 进程ID（PID）               - 区分同一台机器上的不同进程
//   综合以上四要素，可在分布式环境下确保 ID 不重复
// =============================================================================

#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <mutex>
#include <fstream>
#include <unistd.h>   // getpid()
#include "log.hpp"

namespace utils {

// =============================================================================
// 通用 UUID 生成函数
// =============================================================================

/**
 * @brief 生成全局唯一字符串ID
 *
 * 生成格式：{timestamp_sec}_{pid}_{random16}_{counter}
 *   - timestamp_sec: 当前时间戳（秒级）
 *   - pid:           当前进程 ID
 *   - random16:      16 位十六进制随机字符串
 *   - counter:       6 位十进制自增计数器（取模保证长度）
 *
 * 该函数为线程安全，使用静态原子计数器保证多线程调用时 ID 严格唯一。
 *
 * @return std::string 全局唯一ID字符串
 */
inline std::string generate_uuid() {
    // 静态原子计数器，保证进程内严格递增，避免同一毫秒内冲突
    static std::atomic<uint64_t> counter{0};

    // 获取当前时间戳（毫秒级）
    auto now = std::chrono::system_clock::now();
    auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // 获取当前进程 ID
    pid_t pid = getpid();

    // 生成 16 位十六进制随机字符串
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t random_value = dis(gen);

    // 获取自增计数器值
    uint64_t cnt = counter.fetch_add(1, std::memory_order_relaxed);

    // 拼接为唯一ID字符串
    std::ostringstream oss;
    oss << std::hex << ms_since_epoch
        << "_" << std::dec << pid
        << "_" << std::hex << std::setw(16) << std::setfill('0') << random_value
        << "_" << std::dec << std::setw(6) << std::setfill('0') << (cnt % 1000000);
    return oss.str();
}

// =============================================================================
// 文件唯一ID生成器（单例模式）
// =============================================================================

/**
 * @brief 文件唯一ID生成器类（单例模式）
 *
 * 专门为文件子服务设计的唯一ID生成器，采用单例模式确保全局唯一实例。
 *
 * ID 格式设计：file_<timestamp>_<pid>_<random>_<counter>
 *   - 前缀 "file_"：标识此 ID 为文件类型，便于在数据库或日志中识别
 *   - timestamp：毫秒级时间戳，便于按时间排序与排查
 *   - pid：进程ID，避免同一台机器上多个文件服务实例产生冲突
 *   - random：随机数，进一步降低冲突概率
 *   - counter：原子自增计数器，进程内严格递增
 *
 * 使用示例：
 * @code
 * auto& generator = FileIdGenerator::instance();
 * std::string file_id = generator.generate();
 * @endcode
 */
class FileIdGenerator {
public:
    /**
     * @brief 获取单例实例
     *
     * C++11 保证局部静态变量初始化的线程安全性（Magic Statics），
     * 因此无需额外加锁即可保证单例的唯一性。
     *
     * @return FileIdGenerator& 单例实例引用
     */
    static FileIdGenerator& instance() {
        static FileIdGenerator generator;
        return generator;
    }

    /**
     * @brief 生成唯一文件ID
     *
     * 线程安全。综合时间戳、PID、随机数和原子计数器，确保分布式环境下的唯一性。
     *
     * @return std::string 唯一文件ID字符串
     */
    std::string generate() {
        // 获取当前时间戳（毫秒级）
        auto now = std::chrono::system_clock::now();
        auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        // 获取当前进程 ID
        pid_t pid = getpid();

        // 生成 8 位十六进制随机数
        std::uniform_int_distribution<uint32_t> dis;
        uint32_t random_value = dis(rng_);

        // 原子自增计数器（取模 1000000 保证 6 位长度）
        uint64_t cnt = counter_.fetch_add(1, std::memory_order_relaxed) % 1000000;

        // 拼接为文件ID字符串
        std::ostringstream oss;
        oss << "file_"
            << std::hex << ms_since_epoch
            << "_" << std::dec << pid
            << "_" << std::hex << std::setw(8) << std::setfill('0') << random_value
            << "_" << std::dec << std::setw(6) << std::setfill('0') << cnt;
        return oss.str();
    }

    // 禁用拷贝构造、赋值、移动构造、移动赋值，确保单例语义
    FileIdGenerator(const FileIdGenerator&) = delete;
    FileIdGenerator& operator=(const FileIdGenerator&) = delete;
    FileIdGenerator(FileIdGenerator&&) = delete;
    FileIdGenerator& operator=(FileIdGenerator&&) = delete;

private:
    /**
     * @brief 私有构造函数
     *
     * 使用 random_device 初始化随机数生成器，保证每次启动时种子不同。
     */
    FileIdGenerator() : rng_(std::random_device{}()) {}

    std::mt19937 rng_;                  ///< Mersenne Twister 随机数生成器
    std::atomic<uint64_t> counter_{0};  ///< 原子自增计数器，确保多线程下计数器唯一
};

// =============================================================================
// 文件读写工具函数
// =============================================================================

/**
 * @brief 读取文件内容到字符串
 *
 * 以二进制模式读取指定文件的全部内容，适用于读取任意类型的文件
 * （音频、图片、文本等）。该函数为 header-only 实现，无需链接。
 *
 * @param filename 文件路径
 * @param body     输出参数，存放读取到的文件内容
 * @return true 读取成功，false 读取失败
 */
inline bool readFile(const std::string& filename, std::string& body) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file: {}", filename);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    body.resize(size);
    if (!file.read(body.data(), size)) {
        LOG_ERROR("Failed to read file: {}", filename);
        return false;
    }

    LOG_INFO("Read {} bytes from file: {}", size, filename);
    return true;
}

/**
 * @brief 写入字符串内容到文件
 *
 * 以二进制模式将字符串的全部内容写入指定文件。
 * 该函数为 header-only 实现，无需链接。
 *
 * @param filename 文件路径
 * @param body     待写入的文件内容
 * @return true 写入成功，false 写入失败
 */
inline bool writeFile(const std::string& filename, const std::string& body) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: {}", filename);
        return false;
    }

    file.write(body.data(), body.size());
    if (!file.good()) {
        LOG_ERROR("Failed to write file: {}", filename);
        return false;
    }

    LOG_INFO("Wrote {} bytes to file: {}", body.size(), filename);
    return true;
}

} // namespace utils

#endif // UTILS_HPP
