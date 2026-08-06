// =============================================================================
// client_connection_manager.hpp - 客户端长连接管理模块
// =============================================================================
// 本模块管理用户ID与WebSocket长连接句柄的映射关系，支持多设备同时在线。
// 网关服务器通过此模块根据用户ID找到对应的WebSocket连接，进行事件通知推送。
//
// 设计要点：
//   - header-only 实现，无需单独编译
//   - 使用 std::set<connection_hdl> 支持同一用户多设备同时在线
//   - 线程安全：所有操作均通过 std::mutex 保护
//   - 弱引用语义：使用 connection_hdl（弱引用），连接断开后自动失效
//
// 核心功能：
//   1. add_connection    - 用户登录后建立长连接
//   2. remove_connection  - 用户断开连接
//   3. push_notification - 根据用户ID推送通知消息到所有在线设备
//   4. is_user_online    - 查询用户是否在线
// =============================================================================

#ifndef CLIENT_CONNECTION_MANAGER_HPP
#define CLIENT_CONNECTION_MANAGER_HPP

#include <string>
#include <set>
#include <map>
#include <mutex>
#include <memory>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "log.hpp"

namespace gateway {

// WebSocket 服务器类型别名
using WSServer = websocketpp::server<websocketpp::config::asio>;
using ConnectionHdl = websocketpp::connection_hdl;

/**
 * @brief weak_ptr<void> 的自定义比较器
 *
 * websocketpp 的 connection_hdl 底层是 std::weak_ptr<void>，
 * 而 std::weak_ptr 没有默认的 operator<，无法直接用于 std::set。
 * 此比较器通过 owner_before 实现严格弱序，使 std::set 能正确排序连接句柄。
 */
struct ConnectionHdlCompare {
    bool operator()(const ConnectionHdl& lhs, const ConnectionHdl& rhs) const {
        // owner_before 返回 lhs 是否在 rhs 之前（基于内部指针地址）
        return lhs.owner_before(rhs);
    }
};

/**
 * @brief 客户端长连接管理器
 *
 * 维护用户ID到WebSocket连接句柄的映射，支持多设备同时在线。
 * 当网关需要向特定用户推送事件通知时，通过此管理器找到该用户的所有连接，
 * 逐个推送消息。
 *
 * 使用示例：
 * @code
 *   ClientConnectionManager manager;
 *   manager.init(&ws_server);
 *   manager.add_connection("user_123", hdl);
 *   manager.push_notification("user_123", "notification_json");
 * @endcode
 */
class ClientConnectionManager {
public:
    ClientConnectionManager() : ws_server_(nullptr) {}

    ~ClientConnectionManager() {}

    /**
     * @brief 初始化连接管理器
     *
     * 设置 WebSocket 服务器指针，用于后续向客户端推送消息。
     * 必须在 add_connection / push_notification 之前调用。
     *
     * @param ws_server WebSocket 服务器指针
     */
    void init(WSServer* ws_server) {
        ws_server_ = ws_server;
        LOG_INFO("[ClientConnectionManager] Initialized with WebSocket server");
    }

    /**
     * @brief 添加用户连接
     *
     * 用户登录成功后，建立WebSocket长连接时调用。
     * 将用户ID与连接句柄关联，支持同一用户多个设备同时在线。
     *
     * @param user_id 用户ID
     * @param hdl WebSocket连接句柄
     */
    void add_connection(const std::string& user_id, ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(mutex_);
        user_connections_[user_id].insert(hdl);
        LOG_INFO("[ClientConnectionManager] User connected: {}, total connections: {}",
                 user_id, user_connections_[user_id].size());
    }

    /**
     * @brief 移除用户连接
     *
     * WebSocket连接断开时调用，清理对应的连接句柄。
     * 如果用户没有剩余连接，则移除该用户的记录。
     *
     * @param user_id 用户ID
     * @param hdl WebSocket连接句柄
     */
    void remove_connection(const std::string& user_id, ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_connections_.find(user_id);
        if (it != user_connections_.end()) {
            it->second.erase(hdl);
            // 如果该用户没有剩余连接，移除记录
            if (it->second.empty()) {
                user_connections_.erase(it);
                LOG_INFO("[ClientConnectionManager] User fully disconnected: {}", user_id);
            } else {
                LOG_INFO("[ClientConnectionManager] User partially disconnected: {}, remaining: {}",
                         user_id, it->second.size());
            }
        }
    }

    /**
     * @brief 推送通知到指定用户的所有连接
     *
     * 根据用户ID找到所有在线设备的WebSocket连接，逐个发送通知消息。
     * 如果某个连接已断开（弱引用失效），则跳过该连接。
     *
     * @param user_id 目标用户ID
     * @param message 通知消息内容（JSON格式）
     * @return 成功推送到的连接数量
     */
    int push_notification(const std::string& user_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ws_server_) {
            LOG_ERROR("[ClientConnectionManager] WebSocket server not initialized");
            return 0;
        }

        auto it = user_connections_.find(user_id);
        if (it == user_connections_.end()) {
            LOG_DEBUG("[ClientConnectionManager] User not online: {}", user_id);
            return 0;
        }

        int success_count = 0;
        // 遍历该用户的所有连接（多设备支持）
        for (auto hdl : it->second) {
            try {
                // 弱引用检查：如果连接已失效，lock() 返回 nullptr
                auto conn = ws_server_->get_con_from_hdl(hdl);
                if (conn) {
                    conn->send(message, websocketpp::frame::opcode::text);
                    success_count++;
                }
            } catch (const std::exception& e) {
                LOG_WARN("[ClientConnectionManager] Failed to push to user {}: {}", user_id, e.what());
            }
        }

        if (success_count > 0) {
            LOG_INFO("[ClientConnectionManager] Pushed notification to user {}: {} connections",
                     user_id, success_count);
        }
        return success_count;
    }

    /**
     * @brief 检查用户是否在线
     *
     * @param user_id 用户ID
     * @return 至少有一个在线连接返回 true
     */
    bool is_user_online(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_connections_.find(user_id);
        return it != user_connections_.end() && !it->second.empty();
    }

    /**
     * @brief 获取用户在线连接数量
     *
     * @param user_id 用户ID
     * @return 连接数量
     */
    size_t get_connection_count(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_connections_.find(user_id);
        if (it == user_connections_.end()) {
            return 0;
        }
        return it->second.size();
    }

    /**
     * @brief 获取当前在线用户数量
     *
     * @return 在线用户数
     */
    size_t get_online_user_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return user_connections_.size();
    }

private:
    WSServer* ws_server_;                                       ///< WebSocket服务器指针（外部持有）
    std::map<std::string, std::set<ConnectionHdl, ConnectionHdlCompare>> user_connections_;  ///< 用户ID到连接句柄集合的映射
    std::mutex mutex_;                                          ///< 互斥锁，保护并发访问
};

} // namespace gateway

#endif // CLIENT_CONNECTION_MANAGER_HPP
