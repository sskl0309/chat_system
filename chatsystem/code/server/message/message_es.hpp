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
// ES 索引结构（message 索引）：
//   - chat_session_id: keyword 类型（精确匹配会话过滤）
//   - message_id: keyword 类型（精确匹配消息ID，作为文档ID）
//   - content: text 类型（ik_max_word 分词器，支持中文分词搜索）
//
// 搜索查询说明：
//   - 使用 bool must 查询组合多个条件
//   - term 查询：精确匹配 chat_session_id 过滤会话
//   - match 查询：使用 ik_max_word 分词器匹配 content 内容
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
 *
 * 存储消息在 Elasticsearch 中的完整信息，
 * 用于索引操作和搜索结果返回。
 */
struct ESMessage {
    std::string chat_session_id;   // 会话ID（用于过滤）
    std::string message_id;         // 消息ID（作为ES文档ID）
    std::string content;            // 消息文本内容（被索引和搜索）
};

// ES 索引常量
static const char* MESSAGE_DOC_TYPE = "_doc";       // ES 7.x 推荐的文档类型
static const char* MESSAGE_INDEX_NAME = "message";   // 索引名称

/**
 * @brief 将 ESMessage 转换为 Json::Value
 *
 * 用于构造 ES 插入和查询的数据格式。
 * ES 中的字段名与结构体成员名一一对应。
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
 * 提供线程安全的消息插入、搜索、删除等操作。
 */
class MessageES {
public:
    /**
     * @brief 构造函数
     *
     * 初始化 ES 客户端连接。
     * @param es_host ES 主机地址
     * @param es_port ES 端口
     */
    MessageES(const std::string& es_host = "127.0.0.1", int es_port = 9200) {
        std::vector<std::string> hosts;
        hosts.push_back("http://" + es_host + ":" + std::to_string(es_port) + "/");
        client_ = std::make_shared<es::ESClient>(hosts, 3000);  // 超时 3 秒
        LOG_INFO("[MessageES] ES client initialized: {}:{}", es_host, es_port);
    }

    /**
     * @brief 创建消息索引
     *
     * 如果索引已存在则不重建。
     * 字段映射：
     *   - chat_session_id: keyword（用于精确匹配过滤会话）
     *   - message_id: keyword（用于精确匹配消息ID）
     *   - content: text + ik_max_word（支持中文分词搜索）
     *
     * @return true 创建成功或已存在
     */
    bool create_index() {
        std::vector<es::ESClient::FieldProperty> fields = {
            // keyword 类型：精确匹配，不进行分词
            {"chat_session_id", "keyword", "standard", true},
            {"message_id", "keyword", "standard", true},
            // text 类型：ik_max_word 分词器（中文分词）
            {"content", "text", "ik_max_word", true},
        };
        return client_->create_index(MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE, fields);
    }

    /**
     * @brief 新增单条文本消息到 ES
     *
     * 使用 message_id 作为文档 ID，保证幂等性。
     *
     * @param msg 消息信息
     * @return 成功返回 true
     */
    bool insert_message(const ESMessage& msg) {
        return client_->insert_data(
            MESSAGE_INDEX_NAME,
            MESSAGE_DOC_TYPE,
            msg.message_id,                          // 文档ID = 消息ID
            es_message_to_json(msg)
        );
    }

    /**
     * @brief 批量新增文本消息到 ES
     *
     * 逐条插入，遇到失败立即返回 false。
     *
     * @param messages 消息列表
     * @return 全部成功返回 true
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
     * 使用 bool must 查询组合两个条件：
     *   1. term: chat_session_id 精确匹配会话（缩小搜索范围）
     *   2. match: content 匹配关键字（ik_max_word 分词）
     *
     * 搜索流程：
     *   1. 构造 JSON 查询语句
     *   2. 调用 ES 搜索接口
     *   3. 解析搜索结果（hits.hits._source）
     *   4. 转换为 ESMessage 返回
     *
     * @param session_id 会话ID（用于过滤）
     * @param keyword 搜索关键字（用于匹配内容）
     * @param result 输出参数，存储匹配的消息列表
     * @return 搜索成功返回 true
     */
    bool search_by_keyword(const std::string& session_id,
                           const std::string& keyword,
                           std::vector<ESMessage>& result) {
        // 构造查询语句
        Json::Value query;
        // bool must 查询：所有条件都必须满足
        query["query"]["bool"]["must"][0]["term"]["chat_session_id.keyword"] = session_id;
        query["query"]["bool"]["must"][1]["match"]["content"] = keyword;

        Json::Value search_result;
        if (!client_->search_data(MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE, query, search_result)) {
            LOG_ERROR("[MessageES] Search failed, session: {}, keyword: {}", session_id, keyword);
            return false;
        }

        // 解析搜索结果
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
     *
     * @param message_id 消息ID（也是ES文档ID）
     * @return 成功返回 true
     */
    bool delete_message(const std::string& message_id) {
        return client_->delete_data(MESSAGE_INDEX_NAME, MESSAGE_DOC_TYPE, message_id);
    }

    /**
     * @brief 刷新索引
     *
     * 使最近写入的数据立即可被搜索（ES 默认有1秒延迟）。
     *
     * @return 成功返回 true
     */
    bool refresh_index() {
        return client_->refresh_index(MESSAGE_INDEX_NAME);
    }

private:
    // ES 客户端实例
    std::shared_ptr<es::ESClient> client_;
};

} // namespace message_es

#endif // MESSAGE_ES_HPP