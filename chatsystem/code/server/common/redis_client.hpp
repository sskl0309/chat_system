// =============================================================================
// redis_client.hpp - Redis 客户端封装模块
// =============================================================================
// 基于 redis-plus-plus 封装的 Redis 客户端，用于用户子服务的会话管理和验证码管理。
//
// 主要功能：
//   1. 会话管理：登录时创建会话ID-用户ID映射，及用户登录状态标记
//   2. 验证码管理：生成验证码ID-验证码映射，设置过期时间
//
// 依赖说明：
//   - redis-plus-plus: redis++ 客户端库
//   - spdlog: 日志库（通过 log.hpp）
// =============================================================================

#ifndef REDIS_CLIENT_HPP
#define REDIS_CLIENT_HPP

#include <string>
#include <memory>
#include <chrono>
#include <sw/redis++/redis++.h>

#include "log.hpp"

namespace redis_client {

/**
 * @brief Redis 客户端封装类
 *
 * 封装 redis-plus-plus 客户端，提供会话管理和验证码管理功能。
 * 线程安全（redis++ 内部实现线程安全）。
 */
class RedisClient {
public:
    /**
     * @brief 构造函数
     * @param host Redis 服务器地址，默认 "127.0.0.1"
     * @param port Redis 服务器端口，默认 6379
     * @param db Redis 数据库编号，默认 0
     */
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379, int db = 0) {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = port;
        opts.db = db;
        redis_ = std::make_shared<sw::redis::Redis>(opts);
        LOG_INFO("[RedisClient] Connected to Redis: {}:{}, db: {}", host, port, db);
    }

    /**
     * @brief 构造函数（使用连接URL）
     * @param url Redis 连接URL，如 "tcp://127.0.0.1:6379"
     */
    explicit RedisClient(const std::string& url) {
        redis_ = std::make_shared<sw::redis::Redis>(url);
        LOG_INFO("[RedisClient] Connected to Redis: {}", url);
    }

    // ==================== 会话管理 ====================

    /**
     * @brief 创建登录会话
     *
     * 向 Redis 中添加两条映射：
     *   1. session:{session_id} -> {user_id}（会话ID到用户ID的映射）
     *   2. login:{user_id} -> ""（用户登录状态标记，避免重复登录）
     *
     * @param session_id 会话ID
     * @param user_id 用户ID
     * @return 成功返回 true，失败返回 false
     */
    bool create_session(const std::string& session_id, const std::string& user_id) {
        try {
            std::string session_key = "session:" + session_id;
            std::string login_key = "login:" + user_id;

            redis_->set(session_key, user_id);
            redis_->set(login_key, "");
            LOG_INFO("[RedisClient] Session created, session_id: {}, user_id: {}", session_id, user_id);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Create session failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 通过会话ID获取用户ID
     * @param session_id 会话ID
     * @return 用户ID，未找到返回空字符串
     */
    std::string get_user_id_by_session(const std::string& session_id) {
        try {
            std::string session_key = "session:" + session_id;
            auto val = redis_->get(session_key);
            if (val) {
                return *val;
            }
            return "";
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Get user_id by session failed: {}", e.what());
            return "";
        }
    }

    /**
     * @brief 检查用户是否已登录
     * @param user_id 用户ID
     * @return 已登录返回 true，未登录返回 false
     */
    bool is_user_logged_in(const std::string& user_id) {
        try {
            std::string login_key = "login:" + user_id;
            return redis_->exists(login_key) > 0;
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Check login status failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 删除登录会话
     * @param session_id 会话ID
     * @return 成功返回 true
     */
    bool remove_session(const std::string& session_id) {
        try {
            std::string session_key = "session:" + session_id;
            // 先获取 user_id 用于删除登录标记
            auto val = redis_->get(session_key);
            if (val) {
                std::string login_key = "login:" + *val;
                redis_->del(login_key);
            }
            redis_->del(session_key);
            LOG_INFO("[RedisClient] Session removed, session_id: {}", session_id);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Remove session failed: {}", e.what());
            return false;
        }
    }

    // ==================== 验证码管理 ====================

    /**
     * @brief 设置验证码
     *
     * 将验证码ID-验证码映射存入 Redis，并设置过期时间。
     *
     * @param code_id 验证码ID
     * @param code 验证码
     * @param ttl_seconds 过期时间（秒），默认 300 秒（5分钟）
     * @return 成功返回 true
     */
    bool set_verify_code(const std::string& code_id, const std::string& code, int ttl_seconds = 300) {
        try {
            std::string key = "vcode:" + code_id;
            redis_->set(key, code, std::chrono::seconds(ttl_seconds));
            LOG_INFO("[RedisClient] Verify code set, code_id: {}, ttl: {}s", code_id, ttl_seconds);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Set verify code failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取验证码
     * @param code_id 验证码ID
     * @return 验证码字符串，未找到返回空字符串
     */
    std::string get_verify_code(const std::string& code_id) {
        try {
            std::string key = "vcode:" + code_id;
            auto val = redis_->get(key);
            if (val) {
                return *val;
            }
            return "";
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Get verify code failed: {}", e.what());
            return "";
        }
    }

    /**
     * @brief 删除验证码
     *
     * 验证完毕后删除验证码，避免重复使用。
     *
     * @param code_id 验证码ID
     * @return 成功返回 true
     */
    bool remove_verify_code(const std::string& code_id) {
        try {
            std::string key = "vcode:" + code_id;
            redis_->del(key);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("[RedisClient] Remove verify code failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 验证验证码
     *
     * 检查验证码是否匹配，匹配成功后删除验证码。
     *
     * @param code_id 验证码ID
     * @param code 用户输入的验证码
     * @return 匹配返回 true，不匹配或不存在返回 false
     */
    bool verify_code(const std::string& code_id, const std::string& code) {
        std::string stored_code = get_verify_code(code_id);
        if (stored_code.empty()) {
            LOG_WARN("[RedisClient] Verify code not found or expired, code_id: {}", code_id);
            return false;
        }
        if (stored_code != code) {
            LOG_WARN("[RedisClient] Verify code mismatch, code_id: {}", code_id);
            return false;
        }
        // 验证成功，删除验证码
        remove_verify_code(code_id);
        LOG_INFO("[RedisClient] Verify code matched, code_id: {}", code_id);
        return true;
    }

private:
    std::shared_ptr<sw::redis::Redis> redis_;  ///< redis++ 客户端实例
};

} // namespace redis_client

#endif // REDIS_CLIENT_HPP
