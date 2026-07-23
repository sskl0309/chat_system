// =============================================================================
// file_service_impl.hpp - 文件服务 RPC 接口实现声明
// =============================================================================
// 本头文件声明 FileServiceImpl 类，继承自 protobuf 自动生成的 FileService 基类，
// 实现文件上传与下载的 4 个核心 RPC 接口。
//
// 命名空间说明：
//   本类放在 file 命名空间中，与 file.proto / base.proto 的 package file 保持一致，
// 这样可以直接使用 FileService、PutSingleFileReq 等 proto 生成的类型名而无需前缀。
//
// 核心功能：
//   1. GetSingleFile - 根据文件ID下载单个文件
//   2. GetMultiFile  - 根据文件ID列表批量下载文件
//   3. PutSingleFile - 上传单个文件，返回文件元信息（含生成的 file_id）
//   4. PutMultiFile  - 批量上传多个文件
//
// 设计要点：
//   - 通过 FileStorage 抽象接口访问底层存储，便于扩展（策略模式）
//   - 通过 FileIdGenerator 生成唯一文件ID（适配器模式委托给单例）
//   - 使用 RAII（ClosureGuard）确保 RPC 回调一定被执行
//   - 所有错误情况均有详细日志记录，便于排查问题
// =============================================================================

#ifndef FILE_SERVICE_IMPL_HPP
#define FILE_SERVICE_IMPL_HPP

#include <memory>

#include "file.pb.h"
#include "file_storage.hpp"
#include "file_id_generator.hpp"

namespace file {

/**
 * @brief 文件服务 RPC 接口实现类
 *
 * 继承自 protobuf 生成的 FileService 基类，实现文件上传与下载业务逻辑。
 * 通过依赖注入的方式接收 FileStorage 实例，实现业务与存储解耦。
 *
 * 业务流程：
 *   - 上传：生成文件ID -> 调用 FileStorage::store 存储 -> 返回文件元信息
 *   - 下载：解析文件ID -> 调用 FileStorage::read 读取 -> 返回文件二进制内容
 */
class FileServiceImpl : public FileService {
public:
    /**
     * @brief 默认构造函数
     *
     * 创建空的服务实现对象，存储实例需通过 set_storage() 后续设置。
     * 适用于建造者模式中"先创建服务实现，后设置依赖"的场景。
     */
    FileServiceImpl();

    /**
     * @brief 带存储实例的构造函数
     *
     * 创建服务实现并初始化文件存储实例。
     *
     * @param storage 文件存储智能指针（FileStoragePtr = shared_ptr<FileStorage>）
     */
    explicit FileServiceImpl(::file::FileStoragePtr storage);

    /**
     * @brief 虚析构函数
     */
    virtual ~FileServiceImpl();

    /**
     * @brief 设置文件存储实例
     *
     * 在建造者模式中，服务实现对象先于存储实例创建，
     * 因此需要在 build() 完成后通过此方法设置存储实例。
     *
     * @param storage 文件存储智能指针
     */
    void set_storage(::file::FileStoragePtr storage);

public:
    // ==================== RPC 接口实现 ====================

    /**
     * @brief 单文件下载 RPC 接口实现
     *
     * 处理流程：
     *   1. 创建 ClosureGuard 确保 done->Run() 一定被调用
     *   2. 初始化响应消息（设置 request_id 和默认失败状态）
     *   3. 参数校验：检查文件ID是否为空
     *   4. 调用 FileStorage::read 读取文件内容
     *   5. 构造 FileDownloadData 响应数据
     *
     * @param cntl_base RPC 控制器基类指针
     * @param request   客户端请求消息，包含 file_id
     * @param response  服务端响应消息，包含文件二进制内容
     * @param done      回调闭包，处理完毕后必须调用 done->Run()
     */
    virtual void GetSingleFile(google::protobuf::RpcController* cntl_base,
                               const GetSingleFileReq* request,
                               GetSingleFileRsp* response,
                               google::protobuf::Closure* done);

    /**
     * @brief 多文件下载 RPC 接口实现
     *
     * 处理流程：
     *   1. 创建 ClosureGuard 确保回调一定被执行
     *   2. 遍历 file_id_list，逐个调用 FileStorage::read 读取文件
     *   3. 将读取成功的文件填充到响应的 file_data map 中
     *   4. 全部读取完成后设置成功标志
     *
     * @param cntl_base RPC 控制器基类指针
     * @param request   客户端请求消息，包含 file_id_list
     * @param response  服务端响应消息，包含 file_data map
     * @param done      回调闭包
     */
    virtual void GetMultiFile(google::protobuf::RpcController* cntl_base,
                              const GetMultiFileReq* request,
                              GetMultiFileRsp* response,
                              google::protobuf::Closure* done);

    /**
     * @brief 单文件上传 RPC 接口实现
     *
     * 处理流程：
     *   1. 创建 ClosureGuard 确保回调一定被执行
     *   2. 参数校验：检查文件名、文件内容是否为空
     *   3. 调用 FileIdGenerator::generate() 生成唯一文件ID
     *   4. 调用 FileStorage::store 存储文件
     *   5. 构造 FileMessageInfo 响应数据（file_id、文件名、大小）
     *
     * @param cntl_base RPC 控制器基类指针
     * @param request   客户端请求消息，包含 FileUploadData
     * @param response  服务端响应消息，包含 FileMessageInfo
     * @param done      回调闭包
     */
    virtual void PutSingleFile(google::protobuf::RpcController* cntl_base,
                                const PutSingleFileReq* request,
                                PutSingleFileRsp* response,
                                google::protobuf::Closure* done);

    /**
     * @brief 多文件上传 RPC 接口实现
     *
     * 处理流程：
     *   1. 创建 ClosureGuard 确保回调一定被执行
     *   2. 遍历 file_data 列表，对每个文件执行单文件上传逻辑
     *   3. 将上传成功的文件元信息添加到响应的 file_info 列表
     *   4. 全部上传完成后设置成功标志
     *
     * @param cntl_base RPC 控制器基类指针
     * @param request   客户端请求消息，包含 file_data 列表
     * @param response  服务端响应消息，包含 file_info 列表
     * @param done      回调闭包
     */
    virtual void PutMultiFile(google::protobuf::RpcController* cntl_base,
                               const PutMultiFileReq* request,
                               PutMultiFileRsp* response,
                               google::protobuf::Closure* done);

private:
    ::file::FileStoragePtr storage_;   ///< 文件存储实例，用于实际的文件读写操作
};

} // namespace file

#endif // FILE_SERVICE_IMPL_HPP
