// =============================================================================
// file_id_generator.hpp - 文件ID生成器适配层
// =============================================================================
// 本头文件对 common/utils.hpp 中的 utils::FileIdGenerator 进行轻量级适配，
// 为文件子服务提供统一的文件ID生成入口。
//
// 设计模式：适配器模式（Adapter Pattern）
//   - FileIdGenerator 类作为薄封装层，对外提供简洁的 generate() 接口
//   - 内部委托给 utils::FileIdGenerator 单例完成实际 ID 生成工作
//   - 这样设计的好处是：未来若需替换为其他ID生成算法（如雪花算法），
//     只需修改此适配层，不影响业务代码
// =============================================================================

#ifndef FILE_ID_GENERATOR_HPP
#define FILE_ID_GENERATOR_HPP

#include <string>

#include "utils.hpp"

namespace file {

/**
 * @brief 文件ID生成器类
 *
 * 适配 utils::FileIdGenerator 单例，提供简洁的文件ID生成接口。
 * 所有方法均为静态方法，无需创建实例即可使用。
 */
class FileIdGenerator {
public:
    /**
     * @brief 生成唯一文件ID
     *
     * 委托给 utils::FileIdGenerator 单例的 generate() 方法。
     * 生成的ID格式：file_<timestamp>_<pid>_<random>_<counter>
     *
     * @return std::string 唯一文件ID字符串
     */
    static std::string generate() {
        return utils::FileIdGenerator::instance().generate();
    }
};

} // namespace file

#endif // FILE_ID_GENERATOR_HPP
