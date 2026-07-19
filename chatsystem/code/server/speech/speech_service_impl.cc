// 语音识别服务实现类源文件
// 实现语音识别 RPC 接口，通过 SpeechClient 调用百度语音识别 API

#include "speech_service_impl.h"

// brpc 服务器相关头文件，用于 Controller 和 ClosureGuard
#include <brpc/server.h>

namespace speech {

/**
 * @brief 构造函数
 * 
 * 初始化语音识别客户端，传入百度AI开放平台的认证信息。
 * 
 * @param app_id 百度AI开放平台应用的AppID
 * @param api_key API Key，用于获取访问令牌
 * @param secret_key Secret Key，用于获取访问令牌
 */
SpeechServiceImpl::SpeechServiceImpl(const std::string& app_id,
                                     const std::string& api_key,
                                     const std::string& secret_key) {
    // 创建百度语音识别客户端实例
    _speech_client = std::make_unique<speech::SpeechClient>(app_id, api_key, secret_key);
    LOG_INFO("[SpeechServiceImpl] Initialized with app_id: {}", app_id);
}

/**
 * @brief 析构函数
 * 
 * 释放语音识别客户端资源。
 */
SpeechServiceImpl::~SpeechServiceImpl() {
    LOG_INFO("[SpeechServiceImpl] Destroyed");
}

/**
 * @brief 语音识别 RPC 接口实现
 * 
 * 接收客户端发送的语音数据，调用百度语音识别 API 进行识别，
 * 将识别结果返回给客户端。
 * 
 * 处理流程：
 * 1. 初始化响应对象，设置请求ID
 * 2. 校验语音数据是否为空
 * 3. 调用百度语音识别 API 进行识别
 * 4. 根据识别结果构建响应
 * 
 * @param cntl_base RPC 控制器基类指针
 * @param request 语音识别请求，包含语音数据和身份信息
 * @param response 语音识别响应，包含识别状态和识别文本
 * @param done RPC 回调闭包，处理完毕后必须调用 done->Run()
 */
void SpeechServiceImpl::SpeechRecognition(google::protobuf::RpcController* cntl_base,
                                          const SpeechRecognitionReq* request,
                                          SpeechRecognitionRsp* response,
                                          google::protobuf::Closure* done) {
    // ClosureGuard: RAII 守卫，析构时自动调用 done->Run()，确保回调一定被执行
    brpc::ClosureGuard done_guard(done);

    // 将 RpcController 转换为 brpc 的 Controller，以获取更多信息
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    // 设置 HTTP 响应的 Content-Type（通过浏览器访问时生效）
    cntl->http_response().set_content_type("application/json");

    // 设置响应的请求ID，与请求保持一致，便于追踪
    response->set_request_id(request->request_id());

    // 校验语音数据是否为空，为空则直接返回错误
    if (request->speech_content().empty()) {
        response->set_success(false);
        response->set_errmsg("语音数据为空");
        LOG_WARN("[SpeechRecognition] Empty speech content from {}", cntl->remote_side());
        return;
    }

    // 记录接收到的请求信息
    LOG_INFO("[SpeechRecognition] Received request: request_id={}, speech_size={}, user_id={}",
             request->request_id(),
             request->speech_content().size(),
             request->has_user_id() ? request->user_id() : "none");

    // 获取语音数据（直接使用引用，避免不必要的拷贝）
    const std::string& voice_data = request->speech_content();
    
    // 调用百度语音识别 API 进行识别，使用默认参数（wav格式，16kHz采样率）
    speech::RecognizeResult result = _speech_client->recognize_with_result(voice_data);

    // 根据识别结果构建响应
    if (result.code == speech::SpeechResult::SUCCESS) {
        response->set_success(true);
        response->set_recognition_result(result.text);
        LOG_INFO("[SpeechRecognition] Success: request_id={}, result_length={}",
                 request->request_id(),
                 result.text.size());
    } else {
        response->set_success(false);
        // 将状态码转换为可读的错误信息
        response->set_errmsg(speech_result_to_error_message(result.code));
        LOG_ERROR("[SpeechRecognition] Failed: request_id={}, code={}",
                  request->request_id(),
                  static_cast<int>(result.code));
    }
}

/**
 * @brief 将语音识别结果状态码转换为错误信息字符串
 * 
 * 根据 SpeechResult 枚举值，返回对应的中文错误描述信息，
 * 便于客户端理解识别失败的原因。
 * 
 * @param code 语音识别结果状态码
 * @return std::string 对应的错误信息
 */
std::string SpeechServiceImpl::speech_result_to_error_message(SpeechResult code) {
    switch (code) {
        case speech::SpeechResult::EMPTY_RESULT:     // 识别完成但无结果（如音频中无人声）
            return "识别完成但未检测到语音内容";
        case speech::SpeechResult::TOKEN_ERROR:      // Token错误，可能是API Key无效或过期（错误码3300）
            return "Token错误，请检查API Key和Secret Key";
        case speech::SpeechResult::FORMAT_ERROR:     // 音频格式无效（错误码3312）
            return "音频格式无效，请使用wav格式，16kHz采样率";
        case speech::SpeechResult::PERMISSION_ERROR: // 权限错误，应用未开通语音识别服务（错误码502）
            return "权限错误，应用未开通语音识别服务";
        case speech::SpeechResult::NETWORK_ERROR:    // 网络错误，无法连接到百度语音API服务器
            return "网络错误，无法连接到语音识别服务";
        case speech::SpeechResult::UNKNOWN_ERROR:    // 未知错误，其他未预期的错误情况
        default:
            return "未知错误";
    }
}

} // namespace speech
