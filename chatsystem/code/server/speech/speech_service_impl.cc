#include "speech_service_impl.h"
#include "log.hpp"

#include <brpc/controller.h>

namespace speech {

/**
 * @brief 默认构造函数实现
 * 
 * 创建空的服务实现对象，语音客户端未初始化。
 */
SpeechServiceImpl::SpeechServiceImpl() {}

/**
 * @brief 带语音客户端的构造函数实现
 * 
 * 创建服务实现对象并初始化语音客户端。
 * 
 * @param speech_client 语音识别客户端智能指针
 */
SpeechServiceImpl::SpeechServiceImpl(std::shared_ptr<SpeechClient> speech_client)
    : speech_client_(speech_client) {}

/**
 * @brief 析构函数实现
 */
SpeechServiceImpl::~SpeechServiceImpl() {}

/**
 * @brief 设置语音识别客户端实现
 * 
 * 在构建者模式中，服务实现对象先创建，语音客户端在 build() 过程中创建，
 * 因此需要在 build() 完成后通过此方法设置语音客户端。
 * 
 * @param speech_client 语音识别客户端智能指针
 */
void SpeechServiceImpl::set_speech_client(std::shared_ptr<SpeechClient> speech_client) {
    speech_client_ = speech_client;
}

/**
 * @brief 语音识别 RPC 接口实现
 * 
 * 处理流程：
 * 1. 创建 ClosureGuard 确保 done->Run() 一定被调用
 * 2. 设置 HTTP 响应头（Content-Type）
 * 3. 初始化响应消息（设置 request_id 和默认失败状态）
 * 4. 参数校验：检查语音客户端是否已初始化
 * 5. 参数校验：检查语音内容是否为空
 * 6. 调用百度语音识别 API 进行识别
 * 7. 根据识别结果状态码设置响应
 * 8. 处理异常情况
 * 
 * @param cntl_base RPC 控制器基类指针
 * @param request 客户端请求消息
 * @param response 服务端响应消息
 * @param done 回调闭包
 */
void SpeechServiceImpl::SpeechRecognition(google::protobuf::RpcController* cntl_base,
                                          const SpeechRecognitionReq* request,
                                          SpeechRecognitionRsp* response,
                                          google::protobuf::Closure* done) {
    // RAII 守卫：析构时自动调用 done->Run()，确保回调一定被执行
    brpc::ClosureGuard done_guard(done);

    // 将基类控制器转换为 brpc 控制器，以获取更多功能
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    // 设置 HTTP 响应的 Content-Type（通过浏览器访问时生效）
    cntl->http_response().set_content_type("application/json");

    // 初始化响应消息：设置请求ID，默认设置为失败状态
    response->set_request_id(request->request_id());
    response->set_success(false);

    // 校验1：检查语音客户端是否已初始化
    if (!speech_client_) {
        LOG_ERROR("[SpeechServiceImpl] SpeechClient not initialized");
        response->set_errmsg("SpeechClient not initialized");
        return;
    }

    // 校验2：检查语音内容是否为空
    if (request->speech_content().empty()) {
        LOG_WARN("[SpeechServiceImpl] Empty speech content, request_id: {}", request->request_id());
        response->set_errmsg("Empty speech content");
        return;
    }

    // 记录接收到的识别请求信息
    LOG_INFO("[SpeechServiceImpl] Received recognition request, request_id: {}, user_id: {}", 
             request->request_id(), 
             request->has_user_id() ? request->user_id() : "N/A");

    try {
        // 调用语音识别客户端进行识别，获取详细结果（包含错误码）
        RecognizeResult result = speech_client_->recognize_with_result(
            std::string(request->speech_content().data(), request->speech_content().size()));

        // 根据识别结果状态码处理不同情况
        switch (result.code) {
            case SpeechResult::SUCCESS:
                // 识别成功：设置成功标志和识别结果文本
                response->set_success(true);
                response->set_recognition_result(result.text);
                LOG_INFO("[SpeechServiceImpl] Recognition success, request_id: {}, result: {}", 
                         request->request_id(), result.text);
                break;
            
            case SpeechResult::EMPTY_RESULT:
                // 识别完成但无结果（如音频中无人声）：设置成功标志，结果为空字符串
                response->set_success(true);
                response->set_recognition_result("");
                LOG_INFO("[SpeechServiceImpl] Recognition completed with empty result, request_id: {}", 
                         request->request_id());
                break;
            
            case SpeechResult::TOKEN_ERROR:
                // Token 错误（错误码3300）：API Key 无效或过期
                response->set_errmsg("Token error (3300)");
                LOG_ERROR("[SpeechServiceImpl] Token error, request_id: {}", request->request_id());
                break;
            
            case SpeechResult::FORMAT_ERROR:
                // 音频格式错误（错误码3312）：音频格式不支持
                response->set_errmsg("Audio format error (3312)");
                LOG_ERROR("[SpeechServiceImpl] Format error, request_id: {}", request->request_id());
                break;
            
            case SpeechResult::PERMISSION_ERROR:
                // 权限错误（错误码502）：应用未开通语音识别服务
                response->set_errmsg("Permission error (502)");
                LOG_ERROR("[SpeechServiceImpl] Permission error, request_id: {}", request->request_id());
                break;
            
            case SpeechResult::NETWORK_ERROR:
                // 网络错误：无法连接到百度语音API服务器
                response->set_errmsg("Network error");
                LOG_ERROR("[SpeechServiceImpl] Network error, request_id: {}", request->request_id());
                break;
            
            default:
                // 未知错误：其他未预期的错误情况
                response->set_errmsg("Unknown error");
                LOG_ERROR("[SpeechServiceImpl] Unknown error, request_id: {}", request->request_id());
                break;
        }
    } catch (const std::exception& e) {
        // 异常处理：捕获所有未预期的异常
        response->set_errmsg(std::string("Exception: ") + e.what());
        LOG_ERROR("[SpeechServiceImpl] Exception during recognition, request_id: {}, error: {}", 
                  request->request_id(), e.what());
    }
}

}
