// =============================================================================
// message_es.hpp - 消息 ES 数据管理模块
// =============================================================================
// 基于 common/es_client.hpp 二次封装的消息专用 ES 客户端，提供消息的文本存储
// 与关键字搜索功能。
//
// 主要功能：
//   1. 创建消息索引（content 使用 ik_max_word 分词器支持中文搜索）
//   2. 新增文本消息到 ES
//   3. 根据会话ID+关键字搜索消息
//   4. 删除消息
//
// ES 索引结构：
//   - chat_session_id: keyword 类型（精确匹配会话过滤）
//   - message_id: keyword 类型（精确匹配消息ID）
//   - content: text 类型（ik_max_word 分词器，支持中文分词搜索）
//
// 依赖说明：
//   - es_client.hpp: 通用 ES 客户端封装
//   - log.hpp: 日志库
// =============================================================================

#ifndef MESSAGE_ES_HPP
#define MESSAGE_ES_HPP

#include <string>
#include <memory>
#include <vector>
#include <json/json.h>
#include "es_client.hpp"
#include "log.hpp"

namespace message_es {

/**
 * @brief ES 消息信息结构体
 */
struct ESMessage {
    std::string chat_session_id;
    std::string message_id;
    std::string content;
};

static const char* MESSAGE_DOC_TYPE = "_doc";
static const char* MESSAGE_INDEX_NAME = "message";

/**
 * @brief 将 ESMessage 转换为 Json::Value
 */
inline Json::Value es_message_to_json(const ESMessage& msg) {
    Json::Value data;
    data["chat_session_id"] = msg.chat_session_id;
    data["message_id"] = msg.message_id;
    data["content"] = msg.content;
    return data;
}

/**
 * @brief 消息 ES 数据管理类
 *
 * 基于 es::ESClient 封装，管理消息的文本搜索索引。
 */
class MessageES {
public:
    MessageES(const std::string& es_host = "127.0.0.1", int es_port = 9200) {
        std::vector<std::string> hosts;
        hosts.push_back("http://" + es_host + ":" + std::to_string(es_port) + "/");
        client_ = std::make_shared<es::ESClient>(hosts, 3000);
        LOG_INFO("[MessageES] ES client initialized: {}:{}", es_host, es_port);
    }

    /**
     * @brief 创建消息索引
     *
     * 字段映射：
     *   - chat_session_id: keyword（用于精确匹配过滤会话）
     *   - message_id: keyword（用于精确匹配消息ID）
     *   - content: text + ik_max_word（支持中文分词搜索）
     */
    bool create_index() {
        std::vector<es::ESClient::FieldProperty> fields = {
            {"chat_session_id", "keyword", "standard", true},
            {"message_id", "keyword", "standard", true},
            {"content", "text", "ik_max_word", true},
        };
        return client_->create_index(MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE, fields);
    }

    /**
     * @brief 新增文本消息到 ES
     * @param msg 消息信息
     * @return 成功返回 true
     */
    bool insert_message(const ESMessage& msg) {
        return client_->insert_data(
            MESSAGE_INDEX_NAME,
            MESSAGE_DOC_TYPE,
            msg.message_id,
            es_message_to_json(msg)
        );
    }

    /**
     * @brief 批量新增文本消息到 ES
     * @param messages 消息列表
     * @return 成功返回 true
     */
    bool batch_insert(const std::vector<ESMessage>& messages) {
        for (const auto& msg : messages) {
            if (!client_->insert_data(
                    MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE,
                    msg.message_id, es_message_to_json(msg))) {
                LOG_ERROR("[MessageES] Failed to insert message: {}", msg.message_id);
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 根据会话ID和关键字搜索消息
     *
     * 使用 bool must 查询：
     *   - term: chat_session_id 精确匹配会话
     *   - match: content 匹配关键字（ik_max_word 分词）
     *
     * @param session_id 会话ID
     * @param keyword 搜索关键字
     * @param result 输出参数，存储匹配的消息列表
     * @return 搜索成功返回 true
     */
    bool search_by_keyword(const std::string& session_id,
                           const std::string& keyword,
                           std::vector<ESMessage>& result) {
        Json::Value query;
        query["query"]["bool"]["must"][0]["term"]["chat_session_id.keyword"] = session_id;
        query["query"]["bool"]["must"][1]["match"]["content"] = keyword;

        Json::Value search_result;
        if (!client_->search_data(MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE, query, search_result)) {
            LOG_ERROR("[MessageES] Search failed, session: {}, keyword: {}", session_id, keyword);
            return false;
        }

        const Json::Value& hits = search_result["hits"]["hits"];
        for (const auto& hit : hits) {
            ESMessage msg;
            const Json::Value& src = hit["_source"];
            msg.chat_session_id = src.get("chat_session_id", "").asString();
            msg.message_id = src.get("message_id", "").asString();
            msg.content = src.get("content", "").asString();
            result.push_back(msg);
        }

        LOG_INFO("[MessageES] Search completed, session: {}, keyword: {}, hits: {}",
                 session_id, keyword, result.size());
        return true;
    }

    /**
     * @brief 根据消息ID删除消息
     * @param message_id 消息ID
     * @return 成功返回 true
     */
    bool delete_message(const std::string& message_id) {
        return client_->delete_data(MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE, message_id);
    }

    /**
     * @brief 刷新索引（使最近写入的数据立即可被搜索）
     * @return 成功返回 true
     */
    bool refresh_index() {
        return client_->refresh_index(MESSAGE_INDEX_NAME);
    }

private:
    std::shared_ptr<es::ESClient> client_;
};

} // namespace message_es

#endif // MESSAGE_ES_HPP