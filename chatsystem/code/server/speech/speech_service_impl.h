#ifndef SPEECH_SERVICE_IMPL_H
#define SPEECH_SERVICE_IMPL_H

#include <memory>
#include <string>

#include "speech.pb.h"
#include "speech_client.hpp"
#include "log.hpp"

namespace speech {

/**
 * @brief 语音识别服务实现类
 * 
 * 继承自 proto 自动生成的 SpeechService 基类，实现语音识别 RPC 接口。
 * 通过 SpeechClient 调用百度语音识别 API，将语音数据转换为文本。
 */
class SpeechServiceImpl : public SpeechService {
public:
    /**
     * @brief 构造函数
     * 
     * @param app_id 百度AI开放平台应用的AppID
     * @param api_key API Key，用于获取访问令牌
     * @param secret_key Secret Key，用于获取访问令牌
     */
    SpeechServiceImpl(const std::string& app_id, 
                      const std::string& api_key, 
                      const std::string& secret_key);
    
    /**
     * @brief 析构函数
     */
    virtual ~SpeechServiceImpl();

    /**
     * @brief 禁止拷贝构造和赋值
     */
    SpeechServiceImpl(const SpeechServiceImpl&) = delete;
    SpeechServiceImpl& operator=(const SpeechServiceImpl&) = delete;

    /**
     * @brief 语音识别 RPC 接口实现
     * 
     * 接收客户端发送的语音数据，调用百度语音识别 API 进行识别，
     * 将识别结果返回给客户端。
     * 
     * @param cntl_base RPC 控制器基类指针
     * @param request 语音识别请求，包含语音数据和身份信息
     * @param response 语音识别响应，包含识别状态和识别文本
     * @param done RPC 回调闭包，处理完毕后必须调用 done->Run()
     */
    virtual void SpeechRecognition(google::protobuf::RpcController* cntl_base,
                                   const SpeechRecognitionReq* request,
                                   SpeechRecognitionRsp* response,
                                   google::protobuf::Closure* done);

private:
    /**
     * @brief 将语音识别结果状态码转换为错误信息字符串
     * 
     * @param code 语音识别结果状态码
     * @return std::string 对应的错误信息
     */
    std::string speech_result_to_error_message(SpeechResult code);

private:
    std::unique_ptr<speech::SpeechClient> _speech_client;  ///< 语音识别客户端
};

} // namespace speech

#endif // SPEECH_SERVICE_IMPL_H
