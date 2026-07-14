#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <utility>
#include <sstream>

#include <elasticlient/client.h>
#include <cpr/response.h>
#include <json/json.h>

#include "log.hpp"

/**
 * @file es_client.hpp
 * @brief ES客户端API二次封装头文件
 * 
 * 本文件对elasticlient客户端进行了head-only形式的二次封装，提供四个核心操作：
 * 索引创建、数据新增、数据查询、数据删除。
 * 
 * 主要设计目标：
 * 1. 简化ES操作接口，屏蔽底层细节
 * 2. 使用Json::Value对象构造请求体，方便业务层组装数据
 * 3. 统一错误处理机制，捕获异常并记录日志
 * 4. 提供线程安全的访问接口
 * 
 * 使用示例：
 * @code
 * std::vector<std::string> hosts = {"http://localhost:9200/"};
 * es::ESClient client(hosts);
 * 
 * // 创建索引
 * std::vector<es::ESClient::FieldProperty> fields = {
 *     {"name", "text", "ik_max_word", true},
 *     {"age", "integer", "", true}
 * };
 * client.create_index("users", "_doc", fields);
 * 
 * // 新增数据
 * Json::Value data;
 * data["name"] = "张三";
 * data["age"] = 25;
 * client.insert_data("users", "_doc", "1", data);
 * 
 * // 查询数据
 * Json::Value query;
 * query["query"]["match_all"] = Json::objectValue;
 * Json::Value result;
 * client.search_data("users", "_doc", query, result);
 * @endcode
 */

namespace es {

/**
 * @brief ES客户端封装类
 * 
 * 封装elasticlient客户端，提供简洁的ES操作接口。
 * 所有方法均为线程安全，内部使用互斥锁保护并发访问。
 */
class ESClient {
public:
    /**
     * @brief 字段属性结构体
     * 
     * 用于定义索引字段的属性，在创建索引时使用。
     * 每个字段可以指定名称、类型、分词器和是否构造索引。
     */
    struct FieldProperty {
        std::string field_name;   ///< 字段名称，如"name"、"age"
        std::string field_type;   ///< 字段类型，如"text"、"integer"、"keyword"、"date"等
        std::string analyzer;     ///< 分词器类型，如"ik_max_word"、"ik_smart"、"standard"等；为空则使用默认分词器
        bool index;               ///< 是否构造索引，true表示可被搜索，false表示仅存储不索引
    };

    /**
     * @brief 构造函数
     * 
     * 初始化ES客户端，连接到指定的ES集群节点。
     * 
     * @param hosts ES集群节点列表，每个URL必须以"/"结尾，例如{"http://localhost:9200/"}
     * @param timeout 连接超时时间（毫秒），默认3000ms
     */
    ESClient(const std::vector<std::string>& hosts, int timeout = 3000)
        : client_(std::make_shared<elasticlient::Client>(hosts, timeout)) {}

    /**
     * @brief 析构函数
     * 
     * 自动释放elasticlient客户端资源，由shared_ptr自动管理内存。
     */
    ~ESClient() {}

    /**
     * @brief 创建索引
     * 
     * 动态设定索引名称和类型，支持动态添加字段及其属性。
     * 根据固定的JSON格式构造请求体，发送PUT请求创建索引。
     * 
     * 构造的JSON格式如下：
     * @code
     * {
     *     "mappings": {
     *         "{doc_type}": {
     *             "properties": {
     *                 "{field_name}": {
     *                     "type": "{field_type}",
     *                     "analyzer": "{analyzer}",
     *                     "index": {index}
     *                 }
     *             }
     *         }
     *     }
     * }
     * @endcode
     * 
     * @param index_name 索引名称，如"users"、"products"
     * @param doc_type 文档类型，ES 7.x及以上版本建议使用"_doc"
     * @param fields 字段属性列表，每个元素包含字段名、类型、分词器和是否索引
     * @return 创建成功返回true；失败（连接异常、HTTP错误等）返回false
     */
    bool create_index(const std::string& index_name, 
                      const std::string& doc_type,
                      const std::vector<FieldProperty>& fields) {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 构建JSON请求体
            Json::Value root;           // JSON根对象
            Json::Value mappings;       // mappings节点
            Json::Value properties;     // properties节点

            // 遍历所有字段，构建每个字段的属性配置
            for (const auto& field : fields) {
                Json::Value field_value;
                field_value["type"] = field.field_type;           // 设置字段类型
                if (!field.analyzer.empty()) {
                    field_value["analyzer"] = field.analyzer;     // 设置分词器（可选）
                }
                field_value["index"] = field.index;               // 设置是否索引
                properties[field.field_name] = field_value;       // 添加到properties中
            }

            // 组装完整的mappings结构
            mappings[doc_type]["properties"] = properties;
            root["mappings"] = mappings;

            // 将Json::Value序列化为字符串，设置缩进为空以生成紧凑的JSON
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string body = Json::writeString(builder, root);

            // 发送PUT请求创建索引
            cpr::Response response = client_->performRequest(
                elasticlient::Client::HTTPMethod::PUT,
                index_name,
                body
            );

            // 检查HTTP响应状态码，200和201都表示成功
            if (response.status_code == 200 || response.status_code == 201) {
                LOG_INFO("[ESClient] Index created successfully: {}", index_name);
                return true;
            } else {
                LOG_ERROR("[ESClient] Failed to create index: {}, status: {}, error: {}",
                          index_name, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when creating index: {}, message: {}",
                      index_name, e.what());
            return false;
        }
    }

    /**
     * @brief 新增数据
     * 
     * 将Json::Value对象序列化后新增到指定索引。
     * 如果doc_id为空，ES会自动生成文档ID。
     * 
     * @param index_name 索引名称
     * @param doc_type 文档类型
     * @param doc_id 文档ID，为空时由ES自动生成
     * @param data Json::Value对象，包含要新增的数据
     * @return 新增成功返回true；失败返回false
     */
    bool insert_data(const std::string& index_name,
                     const std::string& doc_type,
                     const std::string& doc_id,
                     const Json::Value& data) {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 将Json::Value序列化为紧凑的JSON字符串
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string body = Json::writeString(builder, data);

            // 调用elasticlient的index方法插入数据
            cpr::Response response = client_->index(
                index_name,
                doc_type,
                doc_id,
                body
            );

            // 检查HTTP响应状态码，200和201都表示成功
            if (response.status_code == 200 || response.status_code == 201) {
                LOG_INFO("[ESClient] Data inserted successfully: {}/{}", index_name, doc_id);
                return true;
            } else {
                LOG_ERROR("[ESClient] Failed to insert data: {}/{}, status: {}, error: {}",
                          index_name, doc_id, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when inserting data: {}/{}, message: {}",
                      index_name, doc_id, e.what());
            return false;
        }
    }

    /**
     * @brief 查询数据
     * 
     * 根据查询条件查询数据，返回Json::Value对象。
     * 查询条件以Json::Value形式传入，支持ES的各种查询语法。
     * 
     * 支持的查询示例：
     * @code
     * // match_all查询
     * Json::Value query;
     * query["query"]["match_all"] = Json::objectValue;
     * 
     * // match查询
     * Json::Value query;
     * query["query"]["match"]["name"] = "张三";
     * 
     * // bool查询
     * Json::Value query;
     * query["query"]["bool"]["must"]["match"]["name"] = "张三";
     * query["query"]["bool"]["filter"]["range"]["age"]["gte"] = 18;
     * @endcode
     * 
     * @param index_name 索引名称
     * @param doc_type 文档类型
     * @param query 查询条件，Json::Value对象，遵循ES查询DSL语法
     * @param result 输出参数，存储查询结果，包含完整的ES响应JSON
     * @return 查询成功返回true；失败返回false
     */
    bool search_data(const std::string& index_name,
                     const std::string& doc_type,
                     const Json::Value& query,
                     Json::Value& result) {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 将查询条件序列化为紧凑的JSON字符串
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string body = Json::writeString(builder, query);

            // 调用elasticlient的search方法执行查询
            cpr::Response response = client_->search(
                index_name,
                doc_type,
                body
            );

            // 检查HTTP响应状态码
            if (response.status_code == 200) {
                // 将响应JSON字符串解析为Json::Value对象
                Json::CharReaderBuilder reader_builder;
                std::string errors;
                std::istringstream iss(response.text);
                
                if (Json::parseFromStream(reader_builder, iss, &result, &errors)) {
                    // 兼容ES 7.x+和旧版本的hits.total格式
                    int hit_count = 0;
                    if (result["hits"]["total"].isObject()) {
                        // ES 7.x+格式：{"hits":{"total":{"value":N,"relation":"eq"}}}
                        hit_count = result["hits"]["total"]["value"].asInt();
                    } else {
                        // ES 6.x及以下格式：{"hits":{"total":N}}
                        hit_count = result["hits"]["total"].asInt();
                    }
                    LOG_INFO("[ESClient] Search completed: {}, hits: {}", 
                             index_name, hit_count);
                    return true;
                } else {
                    LOG_ERROR("[ESClient] Failed to parse search result: {}, errors: {}",
                              index_name, errors);
                    return false;
                }
            } else {
                LOG_ERROR("[ESClient] Failed to search data: {}, status: {}, error: {}",
                          index_name, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when searching data: {}, message: {}",
                      index_name, e.what());
            return false;
        }
    }

    /**
     * @brief 根据ID查询数据
     * 
     * 根据文档ID精确查询数据，返回完整的文档内容。
     * 如果文档不存在，返回false，result为空。
     * 
     * @param index_name 索引名称
     * @param doc_type 文档类型
     * @param doc_id 文档ID
     * @param result 输出参数，存储查询结果，包含完整的文档JSON
     * @return 查询成功返回true；文档不存在或失败返回false
     */
    bool get_data(const std::string& index_name,
                  const std::string& doc_type,
                  const std::string& doc_id,
                  Json::Value& result) {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 调用elasticlient的get方法根据ID查询
            cpr::Response response = client_->get(
                index_name,
                doc_type,
                doc_id
            );

            // 检查HTTP响应状态码
            if (response.status_code == 200) {
                // 将响应JSON字符串解析为Json::Value对象
                Json::CharReaderBuilder reader_builder;
                std::string errors;
                std::istringstream iss(response.text);
                
                if (Json::parseFromStream(reader_builder, iss, &result, &errors)) {
                    LOG_INFO("[ESClient] Get data completed: {}/{}", index_name, doc_id);
                    return true;
                } else {
                    LOG_ERROR("[ESClient] Failed to parse get result: {}/{}, errors: {}",
                              index_name, doc_id, errors);
                    return false;
                }
            } else if (response.status_code == 404) {
                // 文档不存在，记录警告日志
                LOG_WARN("[ESClient] Document not found: {}/{}", index_name, doc_id);
                return false;
            } else {
                LOG_ERROR("[ESClient] Failed to get data: {}/{}, status: {}, error: {}",
                          index_name, doc_id, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when getting data: {}/{}, message: {}",
                      index_name, doc_id, e.what());
            return false;
        }
    }

    /**
     * @brief 删除数据
     * 
     * 根据文档ID删除指定数据。
     * 
     * @param index_name 索引名称
     * @param doc_type 文档类型
     * @param doc_id 文档ID
     * @return 删除成功返回true；失败返回false
     */
    bool delete_data(const std::string& index_name,
                     const std::string& doc_type,
                     const std::string& doc_id) {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 调用elasticlient的remove方法删除数据
            cpr::Response response = client_->remove(
                index_name,
                doc_type,
                doc_id
            );

            // 检查HTTP响应状态码
            if (response.status_code == 200) {
                LOG_INFO("[ESClient] Data deleted successfully: {}/{}", index_name, doc_id);
                return true;
            } else {
                LOG_ERROR("[ESClient] Failed to delete data: {}/{}, status: {}, error: {}",
                          index_name, doc_id, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when deleting data: {}/{}, message: {}",
                      index_name, doc_id, e.what());
            return false;
        }
    }

    /**
     * @brief 删除索引
     * 
     * 删除指定的索引，该操作会删除索引中的所有数据，谨慎使用。
     * 
     * @param index_name 索引名称
     * @return 删除成功返回true；失败返回false
     */
    bool delete_index(const std::string& index_name) {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 发送DELETE请求删除索引
            cpr::Response response = client_->performRequest(
                elasticlient::Client::HTTPMethod::DELETE,
                index_name,
                ""  // 删除索引不需要请求体
            );

            // 检查HTTP响应状态码
            if (response.status_code == 200) {
                LOG_INFO("[ESClient] Index deleted successfully: {}", index_name);
                return true;
            } else {
                LOG_ERROR("[ESClient] Failed to delete index: {}, status: {}, error: {}",
                          index_name, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when deleting index: {}, message: {}",
                      index_name, e.what());
            return false;
        }
    }

    /**
     * @brief 刷新索引
     * 
     * 手动刷新指定索引，使最近写入的数据立即可被搜索。
     * ES默认每1秒自动刷新一次，但在写入后立即查询可能搜不到数据，
     * 调用此方法可强制刷新。
     * 
     * @param index_name 索引名称，为空则刷新所有索引
     * @return 刷新成功返回true；失败返回false
     */
    bool refresh_index(const std::string& index_name = "") {
        // 加锁保证线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 构造 _refresh API 路径，index_name 为空时刷新所有索引
            std::string url_path = index_name.empty() ? "_refresh" : index_name + "/_refresh";

            // 发送POST请求刷新索引
            cpr::Response response = client_->performRequest(
                elasticlient::Client::HTTPMethod::POST,
                url_path,
                ""  // 刷新操作不需要请求体
            );

            // 检查HTTP响应状态码
            if (response.status_code == 200) {
                LOG_INFO("[ESClient] Index refreshed: {}", index_name.empty() ? "_all" : index_name);
                return true;
            } else {
                LOG_ERROR("[ESClient] Failed to refresh index: {}, status: {}, error: {}",
                          index_name, response.status_code, response.text);
                return false;
            }
        } catch (const elasticlient::ConnectionException& e) {
            // 捕获连接异常，记录日志并返回失败
            LOG_ERROR("[ESClient] Connection error when refreshing index: {}, message: {}",
                      index_name, e.what());
            return false;
        }
    }

private:
    std::shared_ptr<elasticlient::Client> client_;  ///< elasticlient客户端实例，智能指针管理生命周期
    mutable std::mutex mutex_;                       ///< 互斥锁，保护所有方法的并发访问
};

} // namespace es