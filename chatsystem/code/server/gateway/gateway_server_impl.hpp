// =============================================================================
// gateway_server_impl.hpp - 网关服务器核心实现类声明
// =============================================================================
// 本头文件声明 GatewayServerImpl 类，作为入口网关的核心实现。
// 网关服务器作为客户端与后端子服务之间的中间层，承担以下职责：
//   1. HTTP 请求处理：接收客户端HTTP请求，进行ProtoBuf反序列化、会话鉴权、
//      通过brpc调用对应的子服务，将结果响应给客户端。
//   2. WebSocket 事件通知：维护客户端长连接，在好友申请/处理/删除、
//      会话创建、新消息等事件发生时，主动推送通知给目标客户端。
//
// 架构设计：
//   - httplib 搭建HTTP服务器，接收业务请求
//   - websocketpp 搭建WebSocket服务器，推送事件通知
//   - brpc::ServiceChannelPool 实现服务发现与RPC调用
//   - redis_client::RedisClient 实现会话鉴权
//   - ClientConnectionManager 管理用户长连接
//
// 依赖子服务（通过ServiceChannelPool发现）：
//   - user_service          用户管理服务
//   - friend_management_service 好友管理服务
//   - message_storage_service   消息存储服务
//   - message_transmit_service  消息转发服务
//   - file_service              文件存储服务
//   - speech_service            语音识别服务
// =============================================================================

#ifndef GATEWAY_SERVER_IMPL_HPP
#define GATEWAY_SERVER_IMPL_HPP

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <map>

#include <google/protobuf/message.h>

#include "httplib.h"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "log.hpp"
#include "redis_client.hpp"
#include "brpc_client.hpp"
#include "client_connection_manager.hpp"

// Protobuf 生成的服务桩
#include "user.pb.h"
#include "friend.pb.h"
#include "message.pb.h"
#include "transmit.pb.h"
#include "file.pb.h"
#include "speech.pb.h"
#include "notify.pb.h"

namespace gateway {

/**
 * @brief 网关服务器核心实现类
 *
 * 作为聊天系统的入口网关，负责：
 *   1. HTTP 请求路由与分发：接收客户端请求，鉴权后调用后端子服务
 *   2. WebSocket 事件通知：维护长连接，推送好友/会话/消息事件
 *
 * 使用示例：
 * @code
 *   GatewayServerImpl server;
 *   server.init(http_port, ws_port, redis_client, channel_pool);
 *   server.start();  // 启动HTTP和WebSocket服务器
 * @endcode
 */
class GatewayServerImpl {
public:
    GatewayServerImpl();
    ~GatewayServerImpl();

    // ==================== 初始化方法 ====================

    /**
     * @brief 初始化网关服务器
     *
     * 初始化HTTP服务器路由、WebSocket服务器回调、会话鉴权组件。
     * 必须在 start() 之前调用。
     *
     * @param http_port HTTP服务端口
     * @param ws_port WebSocket服务端口
     * @param redis_client Redis客户端（用于会话鉴权）
     * @param channel_pool RPC信道池（用于服务发现与调用）
     */
    void init(int http_port, int ws_port,
              std::shared_ptr<redis_client::RedisClient> redis_client,
              std::shared_ptr<brpc::ServiceChannelPool> channel_pool);

    /**
     * @brief 启动网关服务器
     *
     * 在独立线程中启动HTTP服务器和WebSocket服务器，开始接收请求。
     * 阻塞运行，直到调用 stop() 或收到退出信号。
     *
     * @return 启动成功返回 true
     */
    bool start();

    /**
     * @brief 停止网关服务器
     *
     * 停止HTTP服务器和WebSocket服务器，释放资源。
     */
    void stop();

    // ==================== 访问器 ====================

    /**
     * @brief 获取HTTP服务器实例（用于配置SSL等高级功能）
     */
    httplib::Server& get_http_server() { return http_server_; }

    /**
     * @brief 获取WebSocket服务器实例
     */
    WSServer& get_ws_server() { return ws_server_; }

    /**
     * @brief 获取客户端连接管理器
     */
    ClientConnectionManager& get_connection_manager() { return conn_manager_; }

private:
    // ==================== HTTP 请求处理方法 ====================

    /**
     * @brief HTTP请求通用处理模板
     *
     * 统一处理流程：解析请求体 -> 鉴权（可选） -> 调用RPC -> 返回响应
     *
     * @tparam ReqProto 请求ProtoBuf类型
     * @tparam RspProto 响应ProtoBuf类型
     * @param req HTTP请求对象
     * @param rsp HTTP响应对象
     * @param require_auth 是否需要会话鉴权
     * @param rpc_handler 业务处理回调（接收已解析的请求ProtoBuf，返回响应ProtoBuf）
     */
    template<typename ReqProto, typename RspProto>
    void handle_rpc_request(const httplib::Request& req, httplib::Response& rsp,
                            bool require_auth,
                            std::function<void(const ReqProto&, RspProto&)> rpc_handler);

    // ==================== 鉴权辅助方法 ====================

    /**
     * @brief 从ProtoBuf消息中获取session_id（使用反射）
     *
     * 通过protobuf反射API动态获取session_id字段，适用于所有包含该字段的消息类型。
     *
     * @param msg ProtoBuf消息对象
     * @param session_id 输出的会话ID
     * @return 成功获取返回 true；字段不存在返回 false
     */
    bool get_session_id_from_proto(const google::protobuf::Message& msg, std::string& session_id);

    /**
     * @brief 向ProtoBuf消息中设置user_id（使用反射）
     *
     * 通过protobuf反射API动态设置user_id字段，适用于所有包含该字段的消息类型。
     *
     * @param msg ProtoBuf消息对象
     * @param user_id 要设置的用户ID
     * @return 成功设置返回 true；字段不存在返回 false
     */
    bool set_user_id_to_proto(google::protobuf::Message& msg, const std::string& user_id);

    /**
     * @brief 校验会话并获取用户ID
     *
     * 从请求ProtoBuf中提取session_id，通过Redis校验会话有效性，
     * 返回对应的用户ID。
     *
     * @param session_id 会话ID
     * @param user_id 输出的用户ID
     * @return 校验成功返回 true
     */
    bool authenticate_session(const std::string& session_id, std::string& user_id);

    // ==================== 通知辅助方法 ====================

    /**
     * @brief 推送好友申请通知
     *
     * 当用户A向用户B发送好友申请时，通知B有新的好友申请。
     *
     * @param target_user_id 目标用户ID（被申请人）
     * @param applicant_info 申请人信息
     */
    void push_friend_apply_notify(const std::string& target_user_id,
                                  const file::UserInfo& applicant_info);

    /**
     * @brief 推送好友处理结果通知
     *
     * 当用户B同意/拒绝用户A的好友申请时，通知A处理结果。
     *
     * @param target_user_id 目标用户ID（申请人）
     * @param agree 是否同意
     * @param handler_info 处理人信息
     */
    void push_friend_process_notify(const std::string& target_user_id,
                                     bool agree,
                                     const file::UserInfo& handler_info);

    /**
     * @brief 推送好友删除通知
     *
     * 当用户A删除用户B的好友时，通知B。
     *
     * @param target_user_id 目标用户ID（被删除者）
     */
    void push_friend_remove_notify(const std::string& target_user_id);

    /**
     * @brief 推送聊天会话创建通知
     *
     * 当新聊天会话创建后，通知会话中的所有成员。
     *
     * @param member_ids 会话成员ID列表
     * @param session_info 会话信息
     */
    void push_chat_session_create_notify(const std::vector<std::string>& member_ids,
                                          const file::ChatSessionInfo& session_info);

    /**
     * @brief 推送新消息通知
     *
     * 当新消息发送后，通知会话中的其他成员。
     *
     * @param target_user_ids 目标用户ID列表
     * @param message_info 消息信息
     */
    void push_new_message_notify(const std::vector<std::string>& target_user_ids,
                                  const file::MessageInfo& message_info);

    // ==================== RPC 调用辅助方法 ====================

    /**
     * @brief 调用用户服务RPC接口
     *
     * @tparam Req 请求类型
     * @tparam Rsp 响应类型
     * @param rpc_name RPC方法名
     * @param req 请求对象
     * @param rsp 响应对象
     * @return 调用成功返回 true
     */
    template<typename Req, typename Rsp>
    bool call_user_service(const std::string& rpc_name, const Req& req, Rsp& rsp);

    /**
     * @brief 调用好友服务RPC接口
     */
    template<typename Req, typename Rsp>
    bool call_friend_service(const std::string& rpc_name, const Req& req, Rsp& rsp);

    /**
     * @brief 调用消息存储服务RPC接口
     */
    template<typename Req, typename Rsp>
    bool call_msg_storage_service(const std::string& rpc_name, const Req& req, Rsp& rsp);

    /**
     * @brief 调用消息转发服务RPC接口
     */
    template<typename Req, typename Rsp>
    bool call_msg_transmit_service(const std::string& rpc_name, const Req& req, Rsp& rsp);

    /**
     * @brief 调用文件服务RPC接口
     */
    template<typename Req, typename Rsp>
    bool call_file_service(const std::string& rpc_name, const Req& req, Rsp& rsp);

    /**
     * @brief 调用语音服务RPC接口
     */
    template<typename Req, typename Rsp>
    bool call_speech_service(const std::string& rpc_name, const Req& req, Rsp& rsp);

    // ==================== 内部数据成员 ====================

    httplib::Server http_server_;                                       ///< HTTP服务器实例
    WSServer ws_server_;                                                ///< WebSocket服务器实例

    std::shared_ptr<redis_client::RedisClient> redis_client_;           ///< Redis客户端（会话鉴权）
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;            ///< RPC信道池（服务发现）
    ClientConnectionManager conn_manager_;                              ///< 客户端连接管理器

    int http_port_;                                                     ///< HTTP服务端口
    int ws_port_;                                                       ///< WebSocket服务端口
    std::atomic<bool> running_;                                         ///< 运行状态标志
    std::thread ws_thread_;                                             ///< WebSocket服务线程
};

} // namespace gateway

#endif // GATEWAY_SERVER_IMPL_HPP
