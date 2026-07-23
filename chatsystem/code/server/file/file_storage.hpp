// =============================================================================
// file_storage.hpp - 文件存储抽象接口（策略模式）
// =============================================================================
// 本头文件定义文件存储的抽象接口（FileStorage），将文件存储行为抽象化，
// 使得上层业务逻辑无需关心底层存储实现细节。
//
// 设计模式：策略模式（Strategy Pattern）
//   - FileStorage 为抽象策略类，定义统一的文件存储接口
//   - LocalFileStorage 为具体策略类，使用本地文件系统存储
//   - 未来可扩展 OSSFileStorage、S3FileStorage 等其他存储后端
//
// 优点：
//   1. 上层业务与底层存储解耦，便于切换存储介质
//   2. 单一职责，每个具体存储类只负责自己的存储细节
//   3. 易于单元测试，可使用内存存储或 Mock 存储进行测试
// =============================================================================

#ifndef FILE_STORAGE_HPP
#define FILE_STORAGE_HPP

#include <string>
#include <memory>

namespace file {

/**
 * @brief 文件元信息结构体
 *
 * 描述一个已存储文件的元数据，包括文件ID、原始文件名、文件大小等。
 * 该结构体在文件上传成功后返回，供业务层使用。
 */
struct FileInfo {
    std::string file_id;        ///< 文件唯一ID
    std::string file_name;      ///< 原始文件名（用户上传时的文件名）
    int64_t     file_size;      ///< 文件大小（字节）
};

/**
 * @brief 文件存储抽象接口类
 *
 * 定义文件存储的核心操作：写入、读取、删除。
 * 具体存储实现需继承此类并实现所有纯虚函数。
 *
 * 使用智能指针管理资源生命周期，避免内存泄漏。
 */
class FileStorage {
public:
    /**
     * @brief 虚析构函数
     *
     * 确保通过基类指针销毁派生类对象时，派生类的析构函数会被正确调用。
     */
    virtual ~FileStorage() = default;

    /**
     * @brief 存储文件
     *
     * 将文件二进制内容写入存储介质，并记录文件元信息。
     *
     * @param file_id      文件唯一ID（由调用方生成）
     * @param file_name    原始文件名
     * @param file_size    文件大小（字节）
     * @param file_content 文件二进制内容
     * @return bool 存储成功返回 true，失败返回 false
     */
    virtual bool store(const std::string& file_id,
                       const std::string& file_name,
                       int64_t file_size,
                       const std::string& file_content) = 0;

    /**
     * @brief 读取文件内容
     *
     * 根据文件ID从存储介质读取文件二进制内容。
     *
     * @param file_id      文件唯一ID
     * @param file_content 输出参数，读取到的文件二进制内容
     * @return bool 读取成功返回 true，失败返回 false（如文件不存在）
     */
    virtual bool read(const std::string& file_id,
                      std::string& file_content) = 0;

    /**
     * @brief 删除文件
     *
     * 根据文件ID从存储介质删除文件。
     *
     * @param file_id 文件唯一ID
     * @return bool 删除成功返回 true，失败返回 false
     */
    virtual bool remove(const std::string& file_id) = 0;

    /**
     * @brief 检查文件是否存在
     *
     * @param file_id 文件唯一ID
     * @return bool 存在返回 true，不存在返回 false
     */
    virtual bool exists(const std::string& file_id) = 0;
};

/**
 * @brief 文件存储智能指针类型别名
 *
 * 使用 shared_ptr 而非 unique_ptr，便于在多个组件间共享存储实例。
 */
using FileStoragePtr = std::shared_ptr<FileStorage>;

} // namespace file

#endif // FILE_STORAGE_HPP
