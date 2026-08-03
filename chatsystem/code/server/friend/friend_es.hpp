// =============================================================================
// friend_es.hpp - 好友搜索 ES 客户端封装
// =============================================================================
// 基于 common/es_client.hpp 与 user_es.hpp 封装的好友搜索专用 ES 客户端。
//
// 功能职责：
//   提供根据关键字搜索用户列表的接口，支持按 user_id / nickname / email 匹配，
//   并可指定排除用户ID列表（例如自己和已是好友的用户）。
//
// 设计说明：
//   本类是对 UserES 的轻量封装，不直接操作 Elasticsearch，
//   而是委托给 UserES 完成实际搜索逻辑。
// =============================================================================

#ifndef FRIEND_ES_HPP
#define FRIEND_ES_HPP

#include <string>
#include <memory>
#include <vector>
#include <json/json.h>

#include "user_es.hpp"
#include "../common/log.hpp"

namespace friend_es {

/// 复用 user_es 中定义的 ESUser 结构体
using ESUser = user_es::ESUser;

/**
 * @brief 好友搜索 ES 客户端
 *
 * 封装 UserES 的搜索接口，增加"排除用户ID列表"等好友搜索场景的逻辑。
 * 在好友搜索场景中，需要排除搜索者自己和已经是好友的用户，
 * 只展示陌生人或非好友用户供添加。
 */
class FriendES {
public:
    /**
     * @brief 构造函数
     *
     * 初始化内部的 UserES 客户端，连接到指定的 Elasticsearch 服务器。
     *
     * @param es_host     Elasticsearch 服务器地址，默认 127.0.0.1
     * @param es_port     Elasticsearch 服务器端口，默认 9200
     * @param index_name  用户索引名称，默认 "user"
     */
    FriendES(const std::string& es_host = "127.0.0.1", int es_port = 9200,
             const std::string& index_name = "user")
        : user_es_(std::make_shared<user_es::UserES>(es_host, es_port, index_name)) {
        LOG_INFO("[FriendES] initialized: {}:{}", es_host, es_port);
    }

    /**
     * @brief 创建用户索引
     *
     * 在 Elasticsearch 中创建用户索引（若不存在），用于存储和搜索用户数据。
     * 通常在服务启动时调用一次。
     *
     * @return true 表示创建成功或索引已存在
     */
    bool create_index() {
        return user_es_->create_index();
    }

    /**
     * @brief 根据关键字搜索用户（排除指定用户）
     *
     * 在 Elasticsearch 中按关键字搜索用户，支持匹配 user_id / nickname / email。
     * 搜索结果中不包含 exclude_uids 列表中的用户。
     *
     * 典型使用场景：好友搜索页面，排除自己和已有好友。
     *
     * @param keyword      搜索关键字（匹配 user_id / nickname / email）
     * @param exclude_uids 需要排除的用户ID列表（自己+已是好友的用户）
     * @param result       输出参数，接收匹配的用户列表
     * @return true 表示搜索成功，false 表示搜索失败或关键字为空
     */
    bool search_user(const std::string& keyword,
                     const std::vector<std::string>& exclude_uids,
                     std::vector<ESUser>& result) {
        if (keyword.empty()) {
            return false;
        }
        return user_es_->search_by_user_id(keyword, result, exclude_uids);
    }

private:
    std::shared_ptr<user_es::UserES> user_es_;  ///< 底层 UserES 客户端实例
};

} // namespace friend_es

#endif
