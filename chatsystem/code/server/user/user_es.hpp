// =============================================================================
// user_es.hpp - 用户 ES 数据管理模块
// =============================================================================
// 基于 common/es_client.hpp 二次封装的用户专用 ES 客户端，提供以 ESUser
// 结构体为输入/输出的便捷接口，屏蔽底层 ES DSL 细节。
//
// 主要功能：
//   1. 创建用户索引（nickname 使用 ik_max_word 分词器）
//   2. 新增/更新用户文档到 ES
//   3. 根据关键字搜索用户（命中 nickname / user_id / email）
//   4. 删除用户文档
//
// 依赖说明：
//   - es_client.hpp: 通用 ES 客户端封装（elasticlient + cpr + jsoncpp）
//   - log.hpp: 日志库
// =============================================================================

#ifndef USER_ES_HPP
#define USER_ES_HPP

#include <string>
#include <memory>
#include <vector>
#include <json/json.h>
#include "es_client.hpp"
#include "log.hpp"

namespace user_es {

/**
 * @brief ES 用户信息结构体
 */
struct ESUser {
    std::string user_id;       ///< 用户ID
    std::string nickname;      ///< 用户昵称
    std::string email;         ///< 用户邮箱
    std::string description;   ///< 用户签名
    std::string avatar_id;     ///< 用户头像文件ID
};

/// 用户索引的文档类型（ES 7.x+ 统一使用 _doc）
static const char* USER_DOC_TYPE = "_doc";

/**
 * @brief 将 ESUser 转换为 Json::Value
 *
 * 用于调用 ESClient 底层接口时的数据转换。
 */
inline Json::Value es_user_to_json(const ESUser& user_info) {
    Json::Value data;
    data["user_id"] = user_info.user_id;
    data["nickname"] = user_info.nickname;
    data["email"] = user_info.email;
    data["description"] = user_info.description;
    data["avatar_id"] = user_info.avatar_id;
    return data;
}

/**
 * @brief ES 用户数据管理类
 *
 * 基于 es::ESClient 封装，管理用户信息的搜索索引。
 * 提供以 ESUser 为粒度的便捷方法，内部委托给通用 ESClient。
 */
class UserES {
public:
    /**
     * @brief 构造函数
     * @param es_host ES 服务器地址，默认 "127.0.0.1"
     * @param es_port ES 服务器端口，默认 9200
     * @param index_name 索引名称，默认 "user"
     */
    UserES(const std::string& es_host = "127.0.0.1", int es_port = 9200,
           const std::string& index_name = "user")
        : index_name_(index_name) {
        // 构造 ESClient 所需的 hosts 列表（必须以 "/" 结尾）
        std::vector<std::string> hosts;
        hosts.push_back("http://" + es_host + ":" + std::to_string(es_port) + "/");
        client_ = std::make_shared<es::ESClient>(hosts, 3000);
        LOG_INFO("[UserES] ES client initialized: {}:{}", es_host, es_port);
    }

    /**
     * @brief 创建用户索引
     *
     * 使用 FieldProperty 定义各字段的映射规则：
     *   - nickname: text + ik_max_word 分词器（支持中文分词搜索）
     *   - user_id:  keyword（精确匹配）
     *   - email:    keyword（精确匹配）
     *   - description / avatar_id: text 但不索引（仅存储）
     *
     * @return 创建成功返回 true
     */
    bool create_index() {
        std::vector<es::ESClient::FieldProperty> fields = {
            {"nickname",    "text",    "ik_max_word", true},
            {"user_id",     "keyword", "",            true},
            {"email",       "keyword", "",            true},
            {"description", "text",    "",            false},
            {"avatar_id",   "text",    "",            false},
        };
        return client_->create_index(index_name_, USER_DOC_TYPE, fields);
    }

    /**
     * @brief 新增用户文档到 ES
     *
     * 将 ESUser 转换为 JSON 后插入，使用 user_id 作为文档 _id。
     *
     * @param user_info 用户信息
     * @return 成功返回 true
     */
    bool insert_data(const ESUser& user_info) {
        return client_->insert_data(
            index_name_,
            USER_DOC_TYPE,
            user_info.user_id,
            es_user_to_json(user_info)
        );
    }

    /**
     * @brief 更新 ES 中的用户文档（覆盖写入）
     *
     * ES 中使用相同的 _id 写入即为覆盖更新。
     *
     * @param user_info 用户信息
     * @return 成功返回 true
     */
    bool update_data(const ESUser& user_info) {
        return insert_data(user_info);
    }

    /**
     * @brief 根据关键字搜索用户
     *
     * 使用 bool should 查询，同时匹配 nickname / user_id / email 三个字段，
     * 任一命中即返回。搜索结果解析为 ESUser 列表。
     *
     * @param keyword      搜索关键字（支持按昵称/用户ID/邮箱搜索）
     * @param result       输出参数，存储匹配的用户列表
     * @param exclude_uids 排除的用户ID列表（如当前用户自己、黑名单用户），通过 must_not term 过滤
     * @return 搜索成功返回 true
     */
    bool search_by_user_id(const std::string& keyword, std::vector<ESUser>& result,
                            const std::vector<std::string>& exclude_uids = {}) {
        // 构造 ES bool should 查询：三字段或匹配
        Json::Value query;
        query["query"]["bool"]["should"][0]["match"]["nickname"] = keyword;
        query["query"]["bool"]["should"][1]["match"]["user_id"] = keyword;
        query["query"]["bool"]["should"][2]["match"]["email"] = keyword;

        // 排除指定用户列表（如：自己、黑名单用户）
        if (!exclude_uids.empty()) {
            for (size_t i = 0; i < exclude_uids.size(); ++i) {
                query["query"]["bool"]["must_not"][static_cast<int>(i)]["term"]["user_id"] = exclude_uids[i];
            }
        }

        // 委托给通用 ESClient
        Json::Value search_result;
        if (!client_->search_data(index_name_, USER_DOC_TYPE, query, search_result)) {
            return false;
        }

        // 解析 ES 响应，提取用户信息列表
        const Json::Value& hits = search_result["hits"]["hits"];
        for (const auto& hit : hits) {
            ESUser user;
            const Json::Value& src = hit["_source"];
            user.user_id     = src.get("user_id", "").asString();
            user.nickname    = src.get("nickname", "").asString();
            user.email       = src.get("email", "").asString();
            user.description = src.get("description", "").asString();
            user.avatar_id   = src.get("avatar_id", "").asString();
            result.push_back(user);
        }

        LOG_INFO("[UserES] Search completed, keyword: {}, hits: {}", keyword, result.size());
        return true;
    }

    /**
     * @brief 根据 user_id 删除用户文档
     * @param user_id 用户ID（同时也是 ES 文档 _id）
     * @return 成功返回 true
     */
    bool delete_data(const std::string& user_id) {
        return client_->delete_data(index_name_, USER_DOC_TYPE, user_id);
    }

private:
    std::string index_name_;                             ///< 索引名称
    std::shared_ptr<es::ESClient> client_;               ///< 通用 ES 客户端（线程安全）
};

} // namespace user_es

#endif // USER_ES_HPP
