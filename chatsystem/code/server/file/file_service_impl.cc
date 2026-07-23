// =============================================================================
// file_service_impl.cc - 文件服务 RPC 接口实现
// =============================================================================
// 本文件实现 file_service_impl.hpp 中声明的 4 个 RPC 接口：
//   1. GetSingleFile - 单文件下载
//   2. GetMultiFile  - 多文件下载
//   3. PutSingleFile - 单文件上传
//   4. PutMultiFile  - 多文件上传
//
// 实现要点：
//   - 所有 RPC 方法使用 brpc::ClosureGuard 确保 done->Run() 一定被调用
//   - 参数校验：检查关键字段是否为空
//   - 错误处理：捕获异常并记录日志，向客户端返回友好的错误信息
//   - 日志记录：记录关键操作日志，便于排查问题
// =============================================================================

#include "file_service_impl.hpp"
#include "log.hpp"

#include <brpc/controller.h>

namespace file {

// =============================================================================
// 构造与析构函数实现
// =============================================================================

/**
 * @brief 默认构造函数实现
 *
 * 创建空的服务实现对象，storage_ 为 nullptr。
 */
FileServiceImpl::FileServiceImpl() {}

/**
 * @brief 带存储实例的构造函数实现
 *
 * @param storage 文件存储智能指针
 */
FileServiceImpl::FileServiceImpl(FileStoragePtr storage)
    : storage_(storage) {}

/**
 * @brief 析构函数实现
 */
FileServiceImpl::~FileServiceImpl() {}

/**
 * @brief 设置文件存储实例实现
 *
 * @param storage 文件存储智能指针
 */
void FileServiceImpl::set_storage(FileStoragePtr storage) {
    storage_ = storage;
}

// =============================================================================
// 单文件下载 RPC 接口实现
// =============================================================================

/**
 * @brief 单文件下载 RPC 接口实现
 *
 * 处理流程：
 *   1. RAII 守卫确保 done->Run() 一定被调用
 *   2. 初始化响应消息（request_id、默认失败状态）
 *   3. 校验参数：检查 storage_ 是否已初始化、file_id 是否为空
 *   4. 调用 storage_->read 读取文件二进制内容
 *   5. 构造 FileDownloadData 响应数据并设置成功标志
 */
void FileServiceImpl::GetSingleFile(google::protobuf::RpcController* cntl_base,
                                    const GetSingleFileReq* request,
                                    GetSingleFileRsp* response,
                                    google::protobuf::Closure* done) {
    // RAII 守卫：析构时自动调用 done->Run()，确保回调一定被执行
    brpc::ClosureGuard done_guard(done);

    // 将基类控制器转换为 brpc 控制器
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;  // 当前实现未使用控制器，避免编译警告

    // 初始化响应消息
    response->set_request_id(request->request_id());
    response->set_success(false);

    // 校验1：检查存储实例是否已初始化
    if (!storage_) {
        LOG_ERROR("[FileServiceImpl] Storage not initialized");
        response->set_errmsg("Storage not initialized");
        return;
    }

    // 校验2：检查文件ID是否为空
    if (request->file_id().empty()) {
        LOG_WARN("[FileServiceImpl] Empty file_id, request_id: {}", request->request_id());
        response->set_errmsg("Empty file_id");
        return;
    }

    LOG_INFO("[FileServiceImpl] GetSingleFile request, request_id: {}, file_id: {}",
             request->request_id(), request->file_id());

    // 调用存储实例读取文件内容
    std::string content;
    if (!storage_->read(request->file_id(), content)) {
        LOG_ERROR("[FileServiceImpl] Failed to read file, file_id: {}", request->file_id());
        response->set_errmsg("File not found or read failed");
        return;
    }

    // 构造响应数据：填充 FileDownloadData
    FileDownloadData* file_data = response->mutable_file_data();
    file_data->set_file_id(request->file_id());
    file_data->set_file_content(content);

    // 设置成功标志
    response->set_success(true);
    LOG_INFO("[FileServiceImpl] GetSingleFile success, file_id: {}, size: {}",
             request->file_id(), content.size());
}

// =============================================================================
// 多文件下载 RPC 接口实现
// =============================================================================

/**
 * @brief 多文件下载 RPC 接口实现
 *
 * 处理流程：
 *   1. RAII 守卫确保回调一定被执行
 *   2. 校验参数：检查 storage_ 是否已初始化、file_id_list 是否为空
 *   3. 遍历 file_id_list，对每个 file_id 调用 storage_->read 读取文件
 *   4. 将读取成功的文件填充到响应的 file_data map 中
 *   5. 设置成功标志（即使部分文件读取失败，整体仍视为成功）
 */
void FileServiceImpl::GetMultiFile(google::protobuf::RpcController* cntl_base,
                                   const GetMultiFileReq* request,
                                   GetMultiFileRsp* response,
                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 校验1：检查存储实例
    if (!storage_) {
        LOG_ERROR("[FileServiceImpl] Storage not initialized");
        response->set_errmsg("Storage not initialized");
        return;
    }

    // 校验2：检查文件ID列表是否为空
    if (request->file_id_list_size() == 0) {
        LOG_WARN("[FileServiceImpl] Empty file_id_list, request_id: {}", request->request_id());
        response->set_errmsg("Empty file_id_list");
        return;
    }

    LOG_INFO("[FileServiceImpl] GetMultiFile request, request_id: {}, file_count: {}",
             request->request_id(), request->file_id_list_size());

    // 获取可写的 file_data map
    auto* file_data_map = response->mutable_file_data();
    int success_count = 0;

    // 遍历文件ID列表，逐个读取文件
    for (int i = 0; i < request->file_id_list_size(); ++i) {
        const std::string& file_id = request->file_id_list(i);

        std::string content;
        if (!storage_->read(file_id, content)) {
            LOG_WARN("[FileServiceImpl] Failed to read file, file_id: {}", file_id);
            continue;   // 单个文件读取失败不影响其他文件
        }

        // 填充 map[file_id] = FileDownloadData
        FileDownloadData& data = (*file_data_map)[file_id];
        data.set_file_id(file_id);
        data.set_file_content(content);
        success_count++;
    }

    // 检查是否所有文件都读取失败
    if (success_count == 0) {
        LOG_ERROR("[FileServiceImpl] All files read failed, request_id: {}", request->request_id());
        response->set_errmsg("All files not found");
        return;
    }

    response->set_success(true);
    LOG_INFO("[FileServiceImpl] GetMultiFile success, request_id: {}, success: {}/{}",
             request->request_id(), success_count, request->file_id_list_size());
}

// =============================================================================
// 单文件上传 RPC 接口实现
// =============================================================================

/**
 * @brief 单文件上传 RPC 接口实现
 *
 * 处理流程：
 *   1. RAII 守卫确保回调一定被执行
 *   2. 校验参数：检查存储实例、文件名、文件内容
 *   3. 调用 FileIdGenerator::generate() 生成唯一文件ID
 *   4. 调用 storage_->store 存储文件二进制内容
 *   5. 构造 FileMessageInfo 响应数据（file_id、文件名、文件大小）
 */
void FileServiceImpl::PutSingleFile(google::protobuf::RpcController* cntl_base,
                                    const PutSingleFileReq* request,
                                    PutSingleFileRsp* response,
                                    google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 校验1：检查存储实例
    if (!storage_) {
        LOG_ERROR("[FileServiceImpl] Storage not initialized");
        response->set_errmsg("Storage not initialized");
        return;
    }

    // 校验2：检查文件数据是否已设置
    if (!request->has_file_data()) {
        LOG_WARN("[FileServiceImpl] No file_data, request_id: {}", request->request_id());
        response->set_errmsg("No file_data");
        return;
    }

    const FileUploadData& upload_data = request->file_data();

    // 校验3：检查文件名是否为空
    if (upload_data.file_name().empty()) {
        LOG_WARN("[FileServiceImpl] Empty file_name, request_id: {}", request->request_id());
        response->set_errmsg("Empty file_name");
        return;
    }

    // 校验4：检查文件内容是否为空
    if (upload_data.file_content().empty()) {
        LOG_WARN("[FileServiceImpl] Empty file_content, request_id: {}", request->request_id());
        response->set_errmsg("Empty file_content");
        return;
    }

    LOG_INFO("[FileServiceImpl] PutSingleFile request, request_id: {}, file_name: {}, size: {}",
             request->request_id(), upload_data.file_name(), upload_data.file_size());

    // 生成唯一文件ID
    std::string file_id = ::file::FileIdGenerator::generate();
    LOG_INFO("[FileServiceImpl] Generated file_id: {}", file_id);

    // 调用存储实例存储文件
    bool store_ok = storage_->store(
        file_id,
        upload_data.file_name(),
        upload_data.file_size(),
        upload_data.file_content());

    if (!store_ok) {
        LOG_ERROR("[FileServiceImpl] Failed to store file, file_id: {}", file_id);
        response->set_errmsg("Failed to store file");
        return;
    }

    // 构造响应数据：填充 FileMessageInfo
    FileMessageInfo* file_info = response->mutable_file_info();
    file_info->set_file_id(file_id);
    file_info->set_file_size(upload_data.file_size());
    file_info->set_file_name(upload_data.file_name());

    response->set_success(true);
    LOG_INFO("[FileServiceImpl] PutSingleFile success, file_id: {}, file_name: {}",
             file_id, upload_data.file_name());
}

// =============================================================================
// 多文件上传 RPC 接口实现
// =============================================================================

/**
 * @brief 多文件上传 RPC 接口实现
 *
 * 处理流程：
 *   1. RAII 守卫确保回调一定被执行
 *   2. 校验参数：检查存储实例、file_data 列表是否为空
 *   3. 遍历 file_data 列表，对每个文件执行单文件上传逻辑
 *   4. 将上传成功的文件元信息添加到响应的 file_info 列表
 *   5. 设置成功标志（即使部分文件上传失败，整体仍视为成功）
 */
void FileServiceImpl::PutMultiFile(google::protobuf::RpcController* cntl_base,
                                    const PutMultiFileReq* request,
                                    PutMultiFileRsp* response,
                                    google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 校验1：检查存储实例
    if (!storage_) {
        LOG_ERROR("[FileServiceImpl] Storage not initialized");
        response->set_errmsg("Storage not initialized");
        return;
    }

    // 校验2：检查文件列表是否为空
    if (request->file_data_size() == 0) {
        LOG_WARN("[FileServiceImpl] Empty file_data list, request_id: {}", request->request_id());
        response->set_errmsg("Empty file_data list");
        return;
    }

    LOG_INFO("[FileServiceImpl] PutMultiFile request, request_id: {}, file_count: {}",
             request->request_id(), request->file_data_size());

    int success_count = 0;

    // 遍历所有上传文件，逐个处理
    for (int i = 0; i < request->file_data_size(); ++i) {
        const FileUploadData& upload_data = request->file_data(i);

        // 跳过空文件名或空内容
        if (upload_data.file_name().empty() || upload_data.file_content().empty()) {
            LOG_WARN("[FileServiceImpl] Skip invalid file at index {}", i);
            continue;
        }

        // 生成唯一文件ID并存储
        std::string file_id = ::file::FileIdGenerator::generate();
        bool store_ok = storage_->store(
            file_id,
            upload_data.file_name(),
            upload_data.file_size(),
            upload_data.file_content());

        if (!store_ok) {
            LOG_ERROR("[FileServiceImpl] Failed to store file at index {}, file_name: {}",
                      i, upload_data.file_name());
            continue;
        }

        // 添加到响应的 file_info 列表
        FileMessageInfo* file_info = response->add_file_info();
        file_info->set_file_id(file_id);
        file_info->set_file_size(upload_data.file_size());
        file_info->set_file_name(upload_data.file_name());

        success_count++;
        LOG_INFO("[FileServiceImpl] Stored file {}/{}, file_id: {}, file_name: {}",
                 i + 1, request->file_data_size(), file_id, upload_data.file_name());
    }

    // 检查是否所有文件都上传失败
    if (success_count == 0) {
        LOG_ERROR("[FileServiceImpl] All files upload failed, request_id: {}",
                  request->request_id());
        response->set_errmsg("All files upload failed");
        return;
    }

    response->set_success(true);
    LOG_INFO("[FileServiceImpl] PutMultiFile success, request_id: {}, success: {}/{}",
             request->request_id(), success_count, request->file_data_size());
}

} // namespace file
