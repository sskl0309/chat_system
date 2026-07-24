// =============================================================================
// user_service_impl.hpp - 用户服务 RPC 接口实现声明
// =============================================================================
// 本头文件声明 UserServiceImpl 类，继承自 protobuf 自动生成的 UserService 基类，
// 实现用户子服务的 11 个核心 RPC 接口。
//
// 命名空间说明：
//   本类放在 chat 命名空间中，与 user.proto 的 package chat 保持一致，
// 这样可以直接使用 UserService、UserRegisterReq 等 proto 生成的类型名而无需前缀。
//
// 核心功能：
//   1. UserRegister        - 用户名注册
//   2. UserLogin           - 用户名登录
//   3. GetEmailVerifyCode  - 获取邮箱验证码
//   4. EmailRegister       - 邮箱注册
//   5. EmailLogin          - 邮箱登录
//   6. GetUserInfo         - 获取用户信息
//   7. GetMultiUserInfo    - 批量获取用户信息
//   8. SetUserAvatar       - 设置用户头像
//   9. SetUserNickname     - 设置用户昵称
//  10. SetUserDescription  - 设置用户签名
//  11. SetUserEmail        - 设置用户邮箱
//
// 依赖组件：
//   - UserTable:       关系型数据库用户数据操作
//   - RedisClient:     会话管理与验证码管理
//   - EmailClient:     邮件发送
//   - UserES:          ES 用户信息搜索索引
//   - FileService:     文件服务 RPC 客户端（头像上传/下载）
// =============================================================================

#ifndef USER_SERVICE_IMPL_HPP
#define USER_SERVICE_IMPL_HPP

#include <memory>
#include <string>

#include "user.pb.h"
#include "user_table.hpp"
#include "redis_client.hpp"
#include "email_client.hpp"
#include "user_es.hpp"
#include "brpc_client.hpp"

namespace chat {

/**
 * @brief 用户服务 RPC 接口实现类
 *
 * 继承自 protobuf 生成的 UserService 基类，实现用户管理业务逻辑。
 * 通过依赖注入的方式接收各组件实例，实现业务与数据访问解耦。
 */
class UserServiceImpl : public UserService {
public:
    /**
     * @brief 默认构造函数
     *
     * 创建空的服务实现对象，各组件需通过 set_xxx() 后续设置。
     */
    UserServiceImpl();

    /**
     * @brief 虚析构函数
     */
    virtual ~UserServiceImpl();

    // ==================== 依赖设置方法 ====================

    /**
     * @brief 设置用户数据库操作实例
     */
    void set_user_table(std::shared_ptr<user_table::UserTable> user_table);

    /**
     * @brief 设置 Redis 客户端实例
     */
    void set_redis_client(std::shared_ptr<redis_client::RedisClient> redis_client);

    /**
     * @brief 设置邮件客户端实例
     */
    void set_email_client(std::shared_ptr<email_client::EmailClient> email_client);

    /**
     * @brief 设置 ES 用户数据管理实例
     */
    void set_user_es(std::shared_ptr<user_es::UserES> user_es);

    /**
     * @brief设置文件服务 RPC 信道池
     */
    void set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool);

public:
    // ==================== RPC 接口实现 ====================

    /// 用户名注册
    virtual void UserRegister(google::protobuf::RpcController* cntl_base,
                              const UserRegisterReq* request,
                              UserRegisterRsp* response,
                              google::protobuf::Closure* done);

    /// 用户名登录
    virtual void UserLogin(google::protobuf::RpcController* cntl_base,
                           const UserLoginReq* request,
                           UserLoginRsp* response,
                           google::protobuf::Closure* done);

    /// 获取邮箱验证码
    virtual void GetEmailVerifyCode(google::protobuf::RpcController* cntl_base,
                                    const EmailVerifyCodeReq* request,
                                    EmailVerifyCodeRsp* response,
                                    google::protobuf::Closure* done);

    /// 邮箱注册
    virtual void EmailRegister(google::protobuf::RpcController* cntl_base,
                               const EmailRegisterReq* request,
                               EmailRegisterRsp* response,
                               google::protobuf::Closure* done);

    /// 邮箱登录
    virtual void EmailLogin(google::protobuf::RpcController* cntl_base,
                            const EmailLoginReq* request,
                            EmailLoginRsp* response,
                            google::protobuf::Closure* done);

    /// 获取用户信息
    virtual void GetUserInfo(google::protobuf::RpcController* cntl_base,
                             const GetUserInfoReq* request,
                             GetUserInfoRsp* response,
                             google::protobuf::Closure* done);

    /// 批量获取用户信息
    virtual void GetMultiUserInfo(google::protobuf::RpcController* cntl_base,
                                  const GetMultiUserInfoReq* request,
                                  GetMultiUserInfoRsp* response,
                                  google::protobuf::Closure* done);

    /// 设置用户头像
    virtual void SetUserAvatar(google::protobuf::RpcController* cntl_base,
                               const SetUserAvatarReq* request,
                               SetUserAvatarRsp* response,
                               google::protobuf::Closure* done);

    /// 设置用户昵称
    virtual void SetUserNickname(google::protobuf::RpcController* cntl_base,
                                 const SetUserNicknameReq* request,
                                 SetUserNicknameRsp* response,
                                 google::protobuf::Closure* done);

    /// 设置用户签名
    virtual void SetUserDescription(google::protobuf::RpcController* cntl_base,
                                    const SetUserDescriptionReq* request,
                                    SetUserDescriptionRsp* response,
                                    google::protobuf::Closure* done);

    /// 设置用户邮箱
    virtual void SetUserEmail(google::protobuf::RpcController* cntl_base,
                              const SetUserEmailReq* request,
                              SetUserEmailRsp* response,
                              google::protobuf::Closure* done);

private:
    // ==================== 私有辅助方法 ====================

    /**
     * @brief 从文件服务获取头像数据
     * @param avatar_id 头像文件ID
     * @param avatar_data 输出参数，存储头像二进制数据
     * @return 成功返回 true
     */
    bool get_avatar_from_file_service(const std::string& avatar_id, std::string& avatar_data);

    /**
     * @brief 上传头像到文件服务
     * @param user_id 用户ID
     * @param avatar_data 头像二进制数据
     * @param avatar_id 输出参数，存储返回的文件ID
     * @return 成功返回 true
     */
    bool upload_avatar_to_file_service(const std::string& user_id,
                                       const std::string& avatar_data,
                                       std::string& avatar_id);

    /**
     * @brief 将数据库用户信息同步到 ES
     * @param u 用户信息
     * @return 成功返回 true
     */
    bool sync_user_to_es(const user_table::UserTable::UserPtr& u);

    std::shared_ptr<user_table::UserTable> user_table_;           ///< 用户数据库操作
    std::shared_ptr<redis_client::RedisClient> redis_client_;     ///< Redis 客户端
    std::shared_ptr<email_client::EmailClient> email_client_;     ///< 邮件客户端
    std::shared_ptr<user_es::UserES> user_es_;                    ///< ES 用户数据管理
    std::shared_ptr<brpc::ServiceChannelPool> channel_pool_;      ///< RPC 信道池（文件服务）
};

} // namespace chat

#endif // USER_SERVICE_IMPL_HPP
