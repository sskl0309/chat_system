#ifndef SPEECH_SERVICE_IMPL_H
#define SPEECH_SERVICE_IMPL_H

#include <memory>

#include "speech.pb.h"
#include "speech_client.hpp"

namespace speech {

/**
 * @brief 语音识别服务实现类
 * 
 * 继承自 protobuf 自动生成的 SpeechService 基类，实现语音识别 RPC 接口。
 * 通过调用百度语音识别 API，将语音数据转换为文本，并返回识别结果。
 * 
 * 核心功能：
 * 1. 接收客户端发送的语音数据请求
 * 2. 调用 SpeechClient 进行语音识别
 * 3. 根据识别结果构建响应消息
 * 4. 处理各种错误情况（凭证错误、格式错误、网络错误等）
 */
class SpeechServiceImpl : public SpeechService {
public:
    /**
     * @brief 默认构造函数
     * 
     * 创建空的服务实现，语音客户端需要后续通过 set_speech_client() 设置。
     */
    SpeechServiceImpl();

    /**
     * @brief 带语音客户端的构造函数
     * 
     * 创建服务实现并初始化语音客户端。
     * 
     * @param speech_client 语音识别客户端智能指针
     */
    explicit SpeechServiceImpl(std::shared_ptr<SpeechClient> speech_client);

    /**
     * @brief 析构函数
     */
    virtual ~SpeechServiceImpl();

    /**
     * @brief 设置语音识别客户端
     * 
     * 在构建者模式中，服务实现对象先于语音客户端创建，因此需要在 build() 
     * 完成后通过此方法设置语音客户端。
     * 
     * @param speech_client 语音识别客户端智能指针
     */
    void set_speech_client(std::shared_ptr<SpeechClient> speech_client);

    /**
     * @brief 语音识别 RPC 接口实现
     * 
     * 这是 speech.proto 中定义的核心 RPC 接口，接收语音数据并返回识别结果。
     * 
     * @param cntl_base RPC 控制器基类指针，用于获取连接信息和设置响应头
     * @param request 客户端发送的请求消息，包含语音数据和身份信息
     * @param response 将要返回给客户端的响应消息，包含识别状态和结果
     * @param done 回调闭包，处理完毕后必须调用 done->Run()
     */
    virtual void SpeechRecognition(google::protobuf::RpcController* cntl_base,
                                   const SpeechRecognitionReq* request,
                                   SpeechRecognitionRsp* response,
                                   google::protobuf::Closure* done);

private:
    std::shared_ptr<SpeechClient> speech_client_;  ///< 语音识别客户端，用于调用百度语音 API
};

}

#endif
