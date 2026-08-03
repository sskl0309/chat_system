// =============================================================================
// friend_es.hpp - 好友搜索 ES 客户端封装
// =============================================================================
// 基于 common/es_client.hpp 与 user_es.hpp 封装的好友搜索专用 ES 客户端。
// 提供根据关键字搜索用户列表的接口，支持按 user_id / nickname / email 匹配，
// 并可指定排除用户ID列表（例如自己和已是好友的用户）。
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

using ESUser = user_es::ESUser;

/**
 * @brief 好友搜索 ES 客户端
 *
 * 封装 UserES 的搜索接口，增加"排除用户ID列表"等好友搜索场景的逻辑。
 */
class FriendES {
public:
    FriendES(const std::string& es_host = "127.0.0.1", int es_port = 9200,
             const std::string& index_name = "user")
        : user_es_(std::make_shared<user_es::UserES>(es_host, es_port, index_name)) {
        LOG_INFO("[FriendES] initialized: {}:{}", es_host, es_port);
    }

    /**
     * @brief 创建用户索引
     */
    bool create_index() {
        return user_es_->create_index();
    }

    /**
     * @brief 根据关键字搜索用户（排除指定用户）
     *
     * @param keyword      搜索关键字（匹配 user_id / nickname / email）
     * @param exclude_uids 需要排除的用户ID（自己+已是好友）
     * @param result       输出匹配的用户列表
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
    std::shared_ptr<user_es::UserES> user_es_;
};

} // namespace friend_es

#endif
