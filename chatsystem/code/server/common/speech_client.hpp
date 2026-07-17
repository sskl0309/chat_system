#ifndef SPEECH_CLIENT_HPP
#define SPEECH_CLIENT_HPP

#include <string>
#include <map>

#include "aip-cpp-sdk-4.16.7/speech.h"

namespace speech {

/**
 * @brief 语音识别结果状态枚举
 * 
 * 用于标识语音识别操作的各种结果状态，方便调用方判断识别是否成功
 * 以及失败时的具体原因。
 */
enum class SpeechResult {
    SUCCESS,          ///< 识别成功，text字段包含识别结果
    EMPTY_RESULT,     ///< 识别完成但无结果（如音频中无人声）
    TOKEN_ERROR,      ///< Token错误，可能是API Key无效或过期（错误码3300）
    FORMAT_ERROR,     ///< 音频格式无效（错误码3312）
    PERMISSION_ERROR, ///< 权限错误，应用未开通语音识别服务（错误码502）
    NETWORK_ERROR,    ///< 网络错误，无法连接到百度语音API服务器
    UNKNOWN_ERROR     ///< 未知错误，其他未预期的错误情况
};

/**
 * @brief 语音识别结果结构体
 * 
 * 包含识别出的文本内容和识别状态码，调用方可通过code字段判断识别结果。
 */
struct RecognizeResult {
    std::string text;     ///< 识别出的文本内容，识别成功时有效
    SpeechResult code;    ///< 识别结果状态码
};

/**
 * @brief SpeechClient 类 - 百度语音识别API的二次封装类
 * 
 * 本类对百度AI开放平台的C++ SDK进行了轻量级封装，提供简洁的语音识别接口。
 * 将复杂的API调用细节、JSON解析和错误处理封装在内部，对外提供直观的接口。
 * 
 * 封装的核心功能：
 * 1. 在构造函数中接收语音识别平台的认证信息（AppID、API Key、Secret Key）
 * 2. 提供语音识别接口，输入语音数据，返回转换后的文字
 * 3. 支持获取详细的识别结果（包含错误码）
 * 
 * @note 本类采用 head-only 方式实现，编译时需链接 curl、crypto 和 jsoncpp 库
 */
class SpeechClient {
public:
    /**
     * @brief 构造函数
     * 
     * 初始化语音识别客户端，传入百度语音API的认证信息。
     * 
     * @param app_id 百度AI开放平台应用的AppID
     * @param api_key API Key，用于获取访问令牌
     * @param secret_key Secret Key，用于获取访问令牌
     */
    SpeechClient(const std::string& app_id, const std::string& api_key, const std::string& secret_key)
        : _client(app_id, api_key, secret_key) {}

    /**
     * @brief 禁止拷贝构造和赋值
     * 
     * 语音客户端内部持有网络连接状态，禁止拷贝以避免资源管理问题。
     */
    SpeechClient(const SpeechClient&) = delete;
    SpeechClient& operator=(const SpeechClient&) = delete;

    /**
     * @brief 语音识别接口（简化版）
     * 
     * 使用默认参数进行语音识别，输入语音数据，直接返回识别出的文本。
     * 默认格式为wav，采样率为16000Hz。
     * 
     * @param voice_data 语音数据的二进制内容
     * @return std::string 识别出的文本，识别失败时返回空字符串
     * @see recognize(const std::string&, const std::string&, int)
     */
    std::string recognize(const std::string& voice_data) {
        RecognizeResult result = recognize_with_result(voice_data);
        return result.text;
    }

    /**
     * @brief 语音识别接口（完整参数版）
     * 
     * 输入语音数据，指定音频格式和采样率，返回识别出的文本。
     * 
     * @param voice_data 语音数据的二进制内容
     * @param format 音频格式，支持 wav、pcm、mp3 等，默认 wav
     * @param rate 采样率，单位Hz，常用值：16000、8000，默认 16000
     * @return std::string 识别出的文本，识别失败时返回空字符串
     * @see recognize_with_result
     */
    std::string recognize(const std::string& voice_data, const std::string& format, int rate) {
        RecognizeResult result = recognize_with_result(voice_data, format, rate);
        return result.text;
    }

    /**
     * @brief 语音识别接口（带详细结果）
     * 
     * 输入语音数据，返回包含文本和错误码的完整结果，便于调用方处理各种异常情况。
     * 
     * @param voice_data 语音数据的二进制内容
     * @param format 音频格式，支持 wav、pcm、mp3 等，默认 wav
     * @param rate 采样率，单位Hz，常用值：16000、8000，默认 16000
     * @return RecognizeResult 包含识别文本和状态码的结果结构体
     * @see SpeechResult
     */
    RecognizeResult recognize_with_result(const std::string& voice_data, 
                                          const std::string& format = "wav", 
                                          int rate = 16000) {
        RecognizeResult result;
        result.code = SpeechResult::UNKNOWN_ERROR;

        if (voice_data.empty()) {
            result.code = SpeechResult::EMPTY_RESULT;
            return result;
        }

        std::map<std::string, std::string> options;
        options["dev_pid"] = "1537";

        Json::Value json_result = _client.recognize(voice_data, format, rate, options);

        if (!json_result.isMember("err_no")) {
            result.code = SpeechResult::NETWORK_ERROR;
            return result;
        }

        int err_no = json_result["err_no"].asInt();

        if (err_no == 0) {
            if (json_result.isMember("result") && !json_result["result"].empty()) {
                for (int i = 0; i < json_result["result"].size(); i++) {
                    result.text += json_result["result"][i].asString();
                }
                result.code = SpeechResult::SUCCESS;
            } else {
                result.code = SpeechResult::EMPTY_RESULT;
            }
        } else if (err_no == 3300) {
            result.code = SpeechResult::TOKEN_ERROR;
        } else if (err_no == 3312) {
            result.code = SpeechResult::FORMAT_ERROR;
        } else if (err_no == 502) {
            result.code = SpeechResult::PERMISSION_ERROR;
        } else {
            result.code = SpeechResult::UNKNOWN_ERROR;
        }

        return result;
    }

private:
    aip::Speech _client; ///< 百度语音SDK的原生客户端对象
};

}

#endif