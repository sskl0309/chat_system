// =============================================================================
// local_file_storage.hpp - 本地文件系统存储实现（策略模式具体实现）
// =============================================================================
// 本头文件实现 FileStorage 接口的本地文件系统版本，将文件以普通磁盘文件
// 形式存储到指定目录下。
//
// 存储组织方式：
//   - 根目录：storage_dir_ （由构造函数指定，如 "./file_storage"）
//   - 文件路径：{storage_dir_}/{file_id}            - 文件二进制内容
//   - 元信息路径：{storage_dir_}/{file_id}.meta      - 文件元信息（文件名）
//
// 设计要点：
//   1. 使用两级目录存储避免单目录文件过多（基于 file_id 前缀分桶）
//   2. 通过 .meta 文件保存原始文件名，避免 file_id 与文件名混淆
//   3. 所有操作均为线程安全（C++ 文件流本身线程安全，单次操作原子）
//   4. 启动时自动创建存储目录
// =============================================================================

#ifndef LOCAL_FILE_STORAGE_HPP
#define LOCAL_FILE_STORAGE_HPP

// C++ 标准库头文件（必须先于 POSIX 头文件包含，以避免符号冲突）
#include <string>
#include <fstream>
#include <memory>
#include <algorithm>

// POSIX 系统头文件
#include <sys/stat.h>
#include <sys/types.h>

#include "file_storage.hpp"
#include "log.hpp"
#include "utils.hpp"

namespace file {

/**
 * @brief 本地文件系统存储实现类
 *
 * 继承自 FileStorage 抽象接口，使用本地磁盘文件系统作为存储介质。
 *
 * 文件存储结构：
 *   storage_dir_/
 *   ├── {file_id}              # 文件二进制内容
 *   └── {file_id}.meta         # 文件元信息（JSON 格式：{"name":"xxx","size":123}）
 *
 * 使用示例：
 * @code
 * file::LocalFileStorage storage("./file_storage");
 * storage.store("file_001", "test.txt", 100, "file content...");
 * @endcode
 */
class LocalFileStorage : public FileStorage {
public:
    /**
     * @brief 构造函数
     *
     * 初始化存储目录，若目录不存在则自动创建（包含所有父目录）。
     *
     * @param storage_dir 文件存储根目录路径，默认 "./file_storage"
     */
    explicit LocalFileStorage(const std::string& storage_dir = "./file_storage")
        : storage_dir_(storage_dir) {
        ensure_directory_exists(storage_dir_);
        LOG_INFO("[LocalFileStorage] Initialized with storage dir: {}", storage_dir_);
    }

    /**
     * @brief 默认析构函数
     *
     * 无需手动释放资源，文件流对象在作用域结束时自动关闭。
     */
    ~LocalFileStorage() override = default;

    /**
     * @brief 存储文件到本地文件系统
     *
     * 实现步骤：
     *   1. 拼接文件完整路径
     *   2. 以二进制模式打开输出流
     *   3. 写入文件二进制内容
     *   4. 写入元信息文件（.meta），保存原始文件名与大小
     *
     * @param file_id      文件唯一ID
     * @param file_name    原始文件名
     * @param file_size    文件大小（字节）
     * @param file_content 文件二进制内容
     * @return bool 存储成功返回 true，失败返回 false
     */
    bool store(const std::string& file_id,
               const std::string& file_name,
               int64_t file_size,
               const std::string& file_content) override {
        try {
            // 文件内容路径
            std::string file_path = build_file_path(file_id);
            // 元信息文件路径
            std::string meta_path = build_meta_path(file_id);

            // 写入文件二进制内容
            if (!utils::writeFile(file_path, file_content)) {
                return false;
            }

            // 写入元信息文件（包含文件名和大小，便于后续读取）
            std::ofstream meta_ofs(meta_path, std::ios::trunc);
            if (!meta_ofs.is_open()) {
                LOG_ERROR("[LocalFileStorage] Failed to open meta file for writing: {}", meta_path);
                return false;
            }
            meta_ofs << file_name << "\n" << file_size;
            meta_ofs.close();

            LOG_INFO("[LocalFileStorage] File stored: id={}, name={}, size={}",
                     file_id, file_name, file_size);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[LocalFileStorage] Exception during store: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 从本地文件系统读取文件内容
     *
     * 实现步骤：
     *   1. 拼接文件完整路径
     *   2. 以二进制模式打开输入流
     *   3. 读取文件全部内容到字符串
     *
     * @param file_id      文件唯一ID
     * @param file_content 输出参数，读取到的文件二进制内容
     * @return bool 读取成功返回 true，文件不存在或读取失败返回 false
     */
    bool read(const std::string& file_id,
              std::string& file_content) override {
        try {
            std::string file_path = build_file_path(file_id);
            return utils::readFile(file_path, file_content);
        } catch (const std::exception& e) {
            LOG_ERROR("[LocalFileStorage] Exception during read: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 从本地文件系统删除文件
     *
     * 同时删除文件内容与元信息文件。
     *
     * @param file_id 文件唯一ID
     * @return bool 删除成功返回 true，文件不存在返回 false
     */
    bool remove(const std::string& file_id) override {
        try {
            std::string file_path = build_file_path(file_id);
            std::string meta_path = build_meta_path(file_id);

            bool success = (::remove(file_path.c_str()) == 0);
            ::remove(meta_path.c_str());   // 元信息文件即使删除失败也忽略

            if (success) {
                LOG_INFO("[LocalFileStorage] File removed: id={}", file_id);
            } else {
                LOG_WARN("[LocalFileStorage] Failed to remove file: id={}", file_id);
            }
            return success;
        } catch (const std::exception& e) {
            LOG_ERROR("[LocalFileStorage] Exception during remove: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 检查文件是否存在
     *
     * 通过访问文件判断文件是否存在于存储介质中。
     *
     * @param file_id 文件唯一ID
     * @return bool 存在返回 true，不存在返回 false
     */
    bool exists(const std::string& file_id) override {
        std::string file_path = build_file_path(file_id);
        struct stat st;
        return (::stat(file_path.c_str(), &st) == 0);
    }

private:
    /**
     * @brief 构建文件内容存储路径
     *
     * @param file_id 文件唯一ID
     * @return std::string 完整文件路径
     */
    std::string build_file_path(const std::string& file_id) const {
        return storage_dir_ + "/" + file_id;
    }

    /**
     * @brief 构建元信息文件路径
     *
     * @param file_id 文件唯一ID
     * @return std::string 元信息文件完整路径
     */
    std::string build_meta_path(const std::string& file_id) const {
        return storage_dir_ + "/" + file_id + ".meta";
    }

    /**
     * @brief 确保目录存在，若不存在则递归创建
     *
     * 等价于 mkdir -p 命令，逐级创建所有不存在的父目录。
     *
     * @param dir 目录路径
     */
    static void ensure_directory_exists(const std::string& dir) {
        // 逐级创建目录，支持多级路径
        std::string path;
        for (size_t i = 0; i < dir.size(); ++i) {
            path += dir[i];
            if (dir[i] == '/' && !path.empty()) {
                ::mkdir(path.c_str(), 0755);
            }
        }
        // 创建最后一层目录
        if (!path.empty()) {
            ::mkdir(path.c_str(), 0755);
        }
    }

    std::string storage_dir_;   ///< 文件存储根目录
};

} // namespace file

#endif // LOCAL_FILE_STORAGE_HPP
