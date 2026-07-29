// =============================================================================
// message_service_impl.cc - 消息存储服务 RPC 接口实现
// =============================================================================
// 本文件实现 MsgStorageServiceImpl 类的所有方法，是消息存储子服务的核心业务逻辑。
//
// 主要功能：
//   1. RPC 接口实现（GetHistoryMsg, GetRecentMsg, MsgSearch）
//   2. MQ 消息消费处理（on_message_consume）
//   3. 外部服务调用（用户信息、文件数据）
//   4. 消息对象构造与转换
// =============================================================================

#include "message_service_impl.hpp"
#include "user.pb.h"
#include "file.pb.h"
#include "message-odb.hxx"
#include "../common/log.hpp"
#include "../common/utils.hpp"

#include <brpc/controller.h>
#include <brpc/closure_guard.h>

namespace message {

// ==================== 构造与析构 ====================

MsgStorageServiceImpl::MsgStorageServiceImpl() {}
MsgStorageServiceImpl::~MsgStorageServiceImpl() {}

// ==================== 依赖注入实现 ====================

void MsgStorageServiceImpl::set_message_table(std::shared_ptr<message_table::MessageTable> table) {
    message_table_ = table;
}

void MsgStorageServiceImpl::set_message_es(std::shared_ptr<message_es::MessageES> es) {
    message_es_ = es;
}

void MsgStorageServiceImpl::set_channel_pool(std::shared_ptr<brpc::ServiceChannelPool> channel_pool) {
    channel_pool_ = channel_pool;
}

void MsgStorageServiceImpl::set_mq_client(std::shared_ptr<mq::MQClient> mq_client) {
    mq_client_ = mq_client;
}

// ==================== 类型转换 ====================

/**
 * @brief 数据库消息类型枚举转 protobuf 枚举
 *
 * 数据库存储的类型定义：
 *   0 - STRING  文本消息
 *   1 - IMAGE   图片消息
 *   2 - FILE    文件消息
 *   3 - SPEECH  语音消息
 */
file::MessageType MsgStorageServiceImpl::convert_message_type(signed char db_type) {
    switch (db_type) {
        case 0: return file::STRING;
        case 1: return file::IMAGE;
        case 2: return file::FILE;
        case 3: return file::SPEECH;
        default: return file::STRING;
    }
}

// ==================== 外部服务调用 ====================

/**
 * @brief 调用用户子服务获取用户信息
 *
 * 通过 RPC 信道池获取 user_service 信道，
 * 调用 GetUserInfo 接口获取指定用户的详细信息。
 *
 * @param user_id 用户ID
 * @param user_info 输出用户信息
 * @return true 调用成功并获取到用户信息
 */
bool MsgStorageServiceImpl::get_user_info(const std::string& user_id, file::UserInfo& user_info) {
    if (!channel_pool_) {
        LOG_ERROR("[MsgStorageServiceImpl] Channel pool not initialized");
        return false;
    }

    // 从信道池获取用户服务信道（基于 etcd 服务发现）
    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[MsgStorageServiceImpl] No user_service channel available");
        return false;
    }

    // 构造 RPC 请求
    chat::UserService_Stub stub(channel.get());
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;

    req.set_request_id(utils::generate_uuid());
    req.set_user_id(user_id);

    // 发起同步 RPC 调用
    brpc::Controller cntl;
    stub.GetUserInfo(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[MsgStorageServiceImpl] Get user info failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    if (rsp.has_user_info()) {
        user_info = rsp.user_info();
        return true;
    }

    return false;
}

/**
 * @brief 调用文件子服务获取文件数据
 *
 * 通过 RPC 信道池获取 file_service 信道，
 * 调用 GetSingleFile 接口获取指定文件的下载数据。
 *
 * @param file_id 文件ID
 * @param file_data 输出文件下载数据（包含文件内容二进制）
 * @return true 调用成功并获取到文件数据
 */
bool MsgStorageServiceImpl::get_file_data(const std::string& file_id, file::FileDownloadData& file_data) {
    if (!channel_pool_) {
        LOG_ERROR("[MsgStorageServiceImpl] Channel pool not initialized");
        return false;
    }

    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) {
        LOG_WARN("[MsgStorageServiceImpl] No file_service channel available, file_id: {}", file_id);
        return false;
    }

    file::FileService_Stub stub(channel.get());
    file::GetSingleFileReq req;
    file::GetSingleFileRsp rsp;

    req.set_request_id(utils::generate_uuid());
    req.set_file_id(file_id);

    brpc::Controller cntl;
    stub.GetSingleFile(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed() || !rsp.success()) {
        LOG_WARN("[MsgStorageServiceImpl] Get file data failed: {}",
                 cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    if (rsp.has_file_data()) {
        file_data = rsp.file_data();
        return true;
    }

    return false;
}

// ==================== 消息对象构造 ====================

/**
 * @brief 从数据库记录构造完整的消息对象
 *
 * 这是消息返回前的核心组装函数，负责：
 *   1. 设置基础字段（消息ID、会话ID、时间戳）
 *   2. 获取发送者用户信息（头像、昵称等）
 *   3. 根据消息类型构造不同的消息体：
 *      - 文本：直接读取数据库 content 字段
 *      - 图片：通过 file_id 从文件子服务获取图片数据
 *      - 文件：从数据库读取文件名、大小等元信息
 *      - 语音：通过 file_id 从文件子服务获取语音数据
 *
 * @param msg_ptr 数据库查询结果的消息记录指针
 * @return file::MessageInfo 可直接返回给客户端的完整消息对象
 */
file::MessageInfo MsgStorageServiceImpl::construct_message_info(std::shared_ptr<msg_record> msg_ptr) {
    file::MessageInfo msg_info;

    // 设置基础标识字段
    msg_info.set_message_id(msg_ptr->message_id());
    msg_info.set_chat_session_id(
        msg_ptr->to_session_id().null() ? "" : msg_ptr->to_session_id().get());

    // 时间转换：数据库字符串 -> 毫秒时间戳
    int64_t timestamp = time_util::string_to_ms(msg_ptr->created_time());
    msg_info.set_timestamp(timestamp);

    // 获取并设置发送者信息
    std::string from_user_id =
        msg_ptr->from_user_id().null() ? "" : msg_ptr->from_user_id().get();
    if (!from_user_id.empty()) {
        file::UserInfo sender_info;
        if (get_user_info(from_user_id, sender_info)) {
            // 成功获取完整用户信息（包含头像、昵称等）
            *msg_info.mutable_sender() = sender_info;
        } else {
            // 仅设置用户ID，其他信息缺失
            msg_info.mutable_sender()->set_user_id(from_user_id);
        }
    }

    // 设置消息类型
    file::MessageContent* content = msg_info.mutable_message();
    content->set_message_type(convert_message_type(msg_ptr->message_type()));

    // 根据消息类型构造对应的消息体
    signed char msg_type = msg_ptr->message_type();

    if (msg_type == 0) {
        // 文本消息：直接从数据库读取内容
        file::StringMessageInfo* str_msg = content->mutable_string_message();
        str_msg->set_content(
            msg_ptr->content().null() ? "" : msg_ptr->content().get());

    } else if (msg_type == 1) {
        // 图片消息：从文件子服务获取图片二进制数据
        file::ImageMessageInfo* img_msg = content->mutable_image_message();
        std::string file_id =
            msg_ptr->file_id().null() ? "" : msg_ptr->file_id().get();
        if (!file_id.empty()) {
            img_msg->set_file_id(file_id);
            file::FileDownloadData file_data;
            if (get_file_data(file_id, file_data)) {
                img_msg->set_image_content(file_data.file_content());
            }
        }

    } else if (msg_type == 2) {
        // 文件消息：从数据库读取元信息
        file::FileMessageInfo* file_msg = content->mutable_file_message();
        std::string file_id =
            msg_ptr->file_id().null() ? "" : msg_ptr->file_id().get();
        if (!file_id.empty()) {
            file_msg->set_file_id(file_id);
            file_msg->set_file_size(
                msg_ptr->filesize().null() ? 0 : msg_ptr->filesize().get());
            file_msg->set_file_name(
                msg_ptr->filename().null() ? "" : msg_ptr->filename().get());
        }

    } else if (msg_type == 3) {
        // 语音消息：从文件子服务获取语音二进制数据
        file::SpeechMessageInfo* speech_msg = content->mutable_speech_message();
        std::string file_id =
            msg_ptr->file_id().null() ? "" : msg_ptr->file_id().get();
        if (!file_id.empty()) {
            speech_msg->set_file_id(file_id);
            file::FileDownloadData file_data;
            if (get_file_data(file_id, file_data)) {
                speech_msg->set_file_contents(file_data.file_content());
            }
        }
    }

    return msg_info;
}

// ==================== 文件消息转储 ====================

/**
 * @brief 将文件/图片/语音消息的数据转储到文件子服务
 *
 * 当 MQ 消费到非文本类型的消息时，需要将消息中携带的文件二进制数据
 * 通过 RPC 上传到文件管理子服务，获取持久化的 file_id。
 *
 * 处理逻辑：
 *   1. 检查消息中是否已有 file_id（已转储过则跳过）
 *   2. 根据消息类型提取文件名、大小、内容
 *   3. 调用文件子服务的 PutSingleFile 接口
 *   4. 记录返回的 file_id
 *
 * @param msg_info 包含待上传文件数据的消息对象
 * @return true 存储成功或无需存储
 */
bool MsgStorageServiceImpl::store_file_message(const file::MessageInfo& msg_info, std::string& out_file_id) {
    out_file_id.clear();

    if (!channel_pool_) {
        LOG_ERROR("[MsgStorageServiceImpl] Channel pool not initialized");
        return false;
    }

    const auto& msg_content = msg_info.message();
    std::string file_id;
    std::string file_name;
    int64_t file_size = 0;

    // 根据消息类型提取已有的文件元信息
    if (msg_content.message_type() == file::IMAGE && msg_content.has_image_message()) {
        const auto& img_msg = msg_content.image_message();
        if (img_msg.has_file_id()) {
            file_id = img_msg.file_id();
        }
    } else if (msg_content.message_type() == file::FILE && msg_content.has_file_message()) {
        const auto& file_msg = msg_content.file_message();
        if (file_msg.has_file_id()) {
            file_id = file_msg.file_id();
        }
        if (file_msg.has_file_name()) {
            file_name = file_msg.file_name();
        }
        if (file_msg.has_file_size()) {
            file_size = file_msg.file_size();
        }
    } else if (msg_content.message_type() == file::SPEECH && msg_content.has_speech_message()) {
        const auto& speech_msg = msg_content.speech_message();
        if (speech_msg.has_file_id()) {
            file_id = speech_msg.file_id();
        }
    }

    // 如果已有 file_id，说明文件已存在于文件子服务中，无需重复上传
    if (!file_id.empty()) {
        out_file_id = file_id;
        return true;
    }

    // 获取文件服务信道
    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) {
        LOG_ERROR("[MsgStorageServiceImpl] No file_service channel available");
        return false;
    }

    // 构造上传请求
    file::FileService_Stub stub(channel.get());
    file::PutSingleFileReq req;
    file::PutSingleFileRsp rsp;

    req.set_request_id(utils::generate_uuid());
    file::FileUploadData* upload_data = req.mutable_file_data();

    // 根据消息类型设置文件上传数据
    if (msg_content.message_type() == file::IMAGE && msg_content.has_image_message()) {
        const auto& img_msg = msg_content.image_message();
        upload_data->set_file_name("image_" + msg_info.message_id());
        upload_data->set_file_size(img_msg.image_content().size());
        upload_data->set_file_content(img_msg.image_content());
    } else if (msg_content.message_type() == file::FILE && msg_content.has_file_message()) {
        const auto& file_msg = msg_content.file_message();
        upload_data->set_file_name(file_msg.file_name());
        upload_data->set_file_size(file_msg.file_size());
        upload_data->set_file_content(file_msg.file_contents());
    } else if (msg_content.message_type() == file::SPEECH && msg_content.has_speech_message()) {
        const auto& speech_msg = msg_content.speech_message();
        upload_data->set_file_name("speech_" + msg_info.message_id());
        upload_data->set_file_size(speech_msg.file_contents().size());
        upload_data->set_file_content(speech_msg.file_contents());
    } else {
        // 文本消息无需上传
        return true;
    }

    // 发送上传请求
    brpc::Controller cntl;
    stub.PutSingleFile(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[MsgStorageServiceImpl] Store file failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    // 记录上传成功的文件ID
    if (rsp.has_file_info()) {
        out_file_id = rsp.file_info().file_id();
        LOG_INFO("[MsgStorageServiceImpl] File stored, file_id: {}", out_file_id);
    }

    return true;
}

// ==================== MQ 消息消费 ====================

/**
 * @brief RabbitMQ 消息消费回调
 *
 * 这是消息存储的核心入口，接收来自消息转发服务的消息，
 * 完成持久化存储的完整流程。
 *
 * 异常处理策略：
 *   - 解析失败 -> reject（不重试，消息格式错误）
 *   - 存储失败 -> reject（可重试，数据库可能暂时不可用）
 *   - 存储成功 -> ack（确认消费，移除消息）
 *
 * @param message_str 消息的 protobuf 序列化字符串
 * @param deliveryTag MQ 投递标签，用于 ack/reject 确认
 */
void MsgStorageServiceImpl::on_message_consume(const std::string& message_str, uint64_t deliveryTag) {
    LOG_INFO("[MsgStorageServiceImpl] Received message from MQ, deliveryTag: {}", deliveryTag);

    // 1. 反序列化消息
    file::MessageInfo msg_info;
    if (!msg_info.ParseFromString(message_str)) {
        LOG_ERROR("[MsgStorageServiceImpl] Failed to parse message from MQ");
        mq_client_->reject(deliveryTag, true);  // 解析失败，不重试
        return;
    }

    LOG_INFO("[MsgStorageServiceImpl] Message parsed, message_id: {}, session: {}",
             msg_info.message_id(), msg_info.chat_session_id());

    // 2. 检查组件是否就绪
    if (!message_table_) {
        LOG_ERROR("[MsgStorageServiceImpl] Message table not initialized");
        mq_client_->reject(deliveryTag, true);
        return;
    }

    try {
        // 3. 构造数据库记录
        msg_record db_msg(msg_info.message_id());
        db_msg.to_session_id(msg_info.chat_session_id());

        // 时间转换：毫秒时间戳 -> 字符串
        std::string time_str = time_util::ms_to_string(msg_info.timestamp());
        db_msg.created_time(time_str);

        // 设置发送者ID
        if (msg_info.has_sender()) {
            db_msg.from_user_id(msg_info.sender().user_id());
        }

        // 设置消息类型
        signed char msg_type = static_cast<signed char>(msg_info.message().message_type());
        db_msg.message_type(msg_type);

        // 4. 根据消息类型填充对应字段
        if (msg_type == 0 && msg_info.message().has_string_message()) {
            // 文本消息：存储内容到数据库
            const auto& str_msg = msg_info.message().string_message();
            db_msg.content(str_msg.content());

            // 同时写入 Elasticsearch 用于关键字搜索
            if (message_es_) {
                message_es::ESMessage es_msg;
                es_msg.chat_session_id = msg_info.chat_session_id();
                es_msg.message_id = msg_info.message_id();
                es_msg.content = str_msg.content();
                message_es_->insert_message(es_msg);
            }
        } else if (msg_type == 1 && msg_info.message().has_image_message()) {
            // 图片消息：存储 file_id
            const auto& img_msg = msg_info.message().image_message();
            if (img_msg.has_file_id()) {
                db_msg.file_id(img_msg.file_id());
            }
        } else if (msg_type == 2 && msg_info.message().has_file_message()) {
            // 文件消息：存储 file_id + 文件名 + 文件大小
            const auto& file_msg = msg_info.message().file_message();
            if (file_msg.has_file_id()) {
                db_msg.file_id(file_msg.file_id());
            }
            if (file_msg.has_file_name()) {
                db_msg.filename(file_msg.file_name());
            }
            if (file_msg.has_file_size()) {
                db_msg.filesize(file_msg.file_size());
            }
        } else if (msg_type == 3 && msg_info.message().has_speech_message()) {
            // 语音消息：存储 file_id
            const auto& speech_msg = msg_info.message().speech_message();
            if (speech_msg.has_file_id()) {
                db_msg.file_id(speech_msg.file_id());
            }
        }

        // 5. 非文本消息：检查是否携带文件数据但无 file_id，需先上传到文件子服务
        if (msg_type != 0) {
            bool has_file_data = false;

            // 检查消息中是否携带了文件的二进制数据
            if (msg_type == 1 && msg_info.message().has_image_message() &&
                msg_info.message().image_message().has_image_content() &&
                !msg_info.message().image_message().image_content().empty()) {
                has_file_data = true;
            } else if (msg_type == 2 && msg_info.message().has_file_message() &&
                       msg_info.message().file_message().has_file_contents() &&
                       !msg_info.message().file_message().file_contents().empty()) {
                has_file_data = true;
            } else if (msg_type == 3 && msg_info.message().has_speech_message() &&
                       msg_info.message().speech_message().has_file_contents() &&
                       !msg_info.message().speech_message().file_contents().empty()) {
                has_file_data = true;
            }

            // 如果有文件数据，上传到文件子服务持久化，获取 file_id
            if (has_file_data) {
                std::string new_file_id;
                if (!store_file_message(msg_info, new_file_id)) {
                    LOG_ERROR("[MsgStorageServiceImpl] Failed to upload file, message_id: {}",
                              msg_info.message_id());
                    mq_client_->reject(deliveryTag, true);  // 上传失败，可重试
                    return;
                }
                // 用文件子服务返回的 file_id 更新数据库记录
                if (!new_file_id.empty()) {
                    db_msg.file_id(new_file_id);
                }
            }
        }

        // 6. 写入 MySQL 数据库
        if (!message_table_->insert(db_msg)) {
            LOG_ERROR("[MsgStorageServiceImpl] Failed to insert message to DB, message_id: {}",
                      msg_info.message_id());
            mq_client_->reject(deliveryTag, true);  // 插入失败，可重试
            return;
        }

        // 7. 确认消费成功
        mq_client_->ack(deliveryTag);
        LOG_INFO("[MsgStorageServiceImpl] Message stored successfully, message_id: {}",
                 msg_info.message_id());

    } catch (const std::exception& e) {
        LOG_ERROR("[MsgStorageServiceImpl] Exception in message consume: {}", e.what());
        mq_client_->reject(deliveryTag, true);  // 异常时拒绝，可重试
    }
}

// ==================== RPC 接口实现 ====================

/**
 * @brief 获取指定时间段的历史消息
 *
 * 接口流程：
 *   1. 参数校验（会话ID、时间范围、数据库组件）
 *   2. 查询数据库获取时间范围内的消息
 *   3. 构造完整消息对象（填充发送者信息、文件数据）
 *   4. 返回结果
 *
 * 使用场景：用户点击"查看更多历史消息"，按时间范围检索聊天记录。
 */
void MsgStorageServiceImpl::GetHistoryMsg(google::protobuf::RpcController* cntl_base,
                                           const GetHistoryMsgReq* request,
                                           GetHistoryMsgRsp* response,
                                           google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 参数校验
    if (request->chat_session_id().empty()) {
        LOG_WARN("[MsgStorageServiceImpl] Missing chat_session_id in GetHistoryMsg");
        response->set_errmsg("Missing chat_session_id");
        return;
    }

    if (request->start_time() <= 0 || request->over_time() <= 0) {
        LOG_WARN("[MsgStorageServiceImpl] Invalid time range in GetHistoryMsg");
        response->set_errmsg("Invalid time range");
        return;
    }

    if (!message_table_) {
        LOG_ERROR("[MsgStorageServiceImpl] Message table not initialized");
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& session_id = request->chat_session_id();
    int64_t start_time_ms = request->start_time();
    int64_t end_time_ms = request->over_time();

    // 时间格式转换：毫秒 -> 字符串
    std::string start_time_str = time_util::ms_to_string(start_time_ms);
    std::string end_time_str = time_util::ms_to_string(end_time_ms);

    // 查询数据库
    auto messages = message_table_->select_by_time_range(session_id, start_time_str, end_time_str);

    // 构造响应消息列表
    for (const auto& msg_ptr : messages) {
        file::MessageInfo msg_info = construct_message_info(msg_ptr);
        *response->add_msg_list() = msg_info;
    }

    response->set_success(true);
    LOG_INFO("[MsgStorageServiceImpl] GetHistoryMsg success, session: {}, count: {}",
             session_id, response->msg_list_size());
}

/**
 * @brief 获取最近N条消息
 *
 * 接口流程：
 *   1. 参数校验（会话ID、消息数量、数据库组件）
 *   2. 可选校验 cur_time 参数（用于分页加载更早的消息）
 *   3. 查询数据库获取最近消息
 *   4. 构造完整消息对象
 *   5. 返回结果
 *
 * 使用场景：
 *   - 登录后打开聊天框显示最近消息（不传 cur_time）
 *   - 向上滚动加载更多历史（传入 cur_time 作为截止时间）
 */
void MsgStorageServiceImpl::GetRecentMsg(google::protobuf::RpcController* cntl_base,
                                          const GetRecentMsgReq* request,
                                          GetRecentMsgRsp* response,
                                          google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 参数校验
    if (request->chat_session_id().empty()) {
        LOG_WARN("[MsgStorageServiceImpl] Missing chat_session_id in GetRecentMsg");
        response->set_errmsg("Missing chat_session_id");
        return;
    }

    if (request->msg_count() <= 0) {
        LOG_WARN("[MsgStorageServiceImpl] Invalid msg_count in GetRecentMsg");
        response->set_errmsg("Invalid msg_count");
        return;
    }

    if (!message_table_) {
        LOG_ERROR("[MsgStorageServiceImpl] Message table not initialized");
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& session_id = request->chat_session_id();
    std::size_t count = static_cast<std::size_t>(request->msg_count());

    // 根据是否有截止时间选择不同的查询方法
    std::vector<std::shared_ptr<msg_record>> messages;

    if (request->has_cur_time() && request->cur_time() > 0) {
        // 分页场景：获取指定时间之前的消息
        std::string before_time_str = time_util::ms_to_string(request->cur_time());
        messages = message_table_->select_recent_before(session_id, count, before_time_str);
    } else {
        // 首次加载：获取最新消息
        messages = message_table_->select_recent(session_id, count);
    }

    // 构造响应
    for (const auto& msg_ptr : messages) {
        file::MessageInfo msg_info = construct_message_info(msg_ptr);
        *response->add_msg_list() = msg_info;
    }

    response->set_success(true);
    LOG_INFO("[MsgStorageServiceImpl] GetRecentMsg success, session: {}, count: {}",
             session_id, response->msg_list_size());
}

/**
 * @brief 关键字消息搜索
 *
 * 接口流程：
 *   1. 参数校验（会话ID、搜索关键字、ES组件、数据库组件）
 *   2. 调用 Elasticsearch 进行全文搜索
 *      - 以会话ID过滤（term 查询）
 *      - 以关键字匹配内容（match 查询，ik_max_word 分词）
 *   3. 根据 ES 返回的消息ID列表，从 MySQL 回查完整信息
 *   4. 构造完整消息对象并返回
 *
 * 使用场景：用户在聊天窗口中搜索特定关键词的聊天记录。
 */
void MsgStorageServiceImpl::MsgSearch(google::protobuf::RpcController* cntl_base,
                                       const MsgSearchReq* request,
                                       MsgSearchRsp* response,
                                       google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

    // 参数校验
    if (request->chat_session_id().empty()) {
        LOG_WARN("[MsgStorageServiceImpl] Missing chat_session_id in MsgSearch");
        response->set_errmsg("Missing chat_session_id");
        return;
    }

    if (request->search_key().empty()) {
        LOG_WARN("[MsgStorageServiceImpl] Empty search_key in MsgSearch");
        response->set_errmsg("Empty search_key");
        return;
    }

    if (!message_es_) {
        LOG_ERROR("[MsgStorageServiceImpl] Message ES not initialized");
        response->set_errmsg("ES not initialized");
        return;
    }

    if (!message_table_) {
        LOG_ERROR("[MsgStorageServiceImpl] Message table not initialized");
        response->set_errmsg("Database not initialized");
        return;
    }

    const std::string& session_id = request->chat_session_id();
    const std::string& search_key = request->search_key();

    // Step 1: 使用 ES 进行关键字搜索
    std::vector<message_es::ESMessage> es_results;
    if (!message_es_->search_by_keyword(session_id, search_key, es_results)) {
        LOG_ERROR("[MsgStorageServiceImpl] ES search failed, session: {}, key: {}",
                  session_id, search_key);
        response->set_errmsg("Search failed");
        return;
    }

    // 无搜索结果，直接返回
    if (es_results.empty()) {
        response->set_success(true);
        LOG_INFO("[MsgStorageServiceImpl] MsgSearch no results, session: {}, key: {}",
                 session_id, search_key);
        return;
    }

    // Step 2: 提取消息ID列表
    std::vector<std::string> message_ids;
    for (const auto& es_msg : es_results) {
        message_ids.push_back(es_msg.message_id);
    }

    // Step 3: 从 MySQL 回查完整消息信息
    auto db_messages = message_table_->select_by_message_ids(message_ids);

    // Step 4: 构造完整响应
    for (const auto& msg_ptr : db_messages) {
        file::MessageInfo msg_info = construct_message_info(msg_ptr);
        *response->add_msg_list() = msg_info;
    }

    response->set_success(true);
    LOG_INFO("[MsgStorageServiceImpl] MsgSearch success, session: {}, key: {}, es_hits: {}, db_hits: {}",
             session_id, search_key, es_results.size(), response->msg_list_size());
}

} // namespace message