// =============================================================================
// message_service_impl.cc - 消息存储服务 RPC 接口实现
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

MsgStorageServiceImpl::MsgStorageServiceImpl() {}
MsgStorageServiceImpl::~MsgStorageServiceImpl() {}

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

file::MessageType MsgStorageServiceImpl::convert_message_type(signed char db_type) {
    switch (db_type) {
        case 0: return file::STRING;
        case 1: return file::IMAGE;
        case 2: return file::FILE;
        case 3: return file::SPEECH;
        default: return file::STRING;
    }
}

bool MsgStorageServiceImpl::get_user_info(const std::string& user_id, file::UserInfo& user_info) {
    if (!channel_pool_) {
        LOG_ERROR("[MsgStorageServiceImpl] Channel pool not initialized");
        return false;
    }

    auto channel = channel_pool_->get_channel("user_service");
    if (!channel) {
        LOG_ERROR("[MsgStorageServiceImpl] No user_service channel available");
        return false;
    }

    chat::UserService_Stub stub(channel.get());
    chat::GetUserInfoReq req;
    chat::GetUserInfoRsp rsp;

    req.set_request_id(utils::generate_uuid());
    req.set_user_id(user_id);

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

file::MessageInfo MsgStorageServiceImpl::construct_message_info(std::shared_ptr<msg_record> msg_ptr) {
    file::MessageInfo msg_info;

    msg_info.set_message_id(msg_ptr->message_id());
    msg_info.set_chat_session_id(
        msg_ptr->to_session_id().null() ? "" : msg_ptr->to_session_id().get());

    int64_t timestamp = time_util::string_to_ms(msg_ptr->created_time());
    msg_info.set_timestamp(timestamp);

    std::string from_user_id =
        msg_ptr->from_user_id().null() ? "" : msg_ptr->from_user_id().get();
    if (!from_user_id.empty()) {
        file::UserInfo sender_info;
        if (get_user_info(from_user_id, sender_info)) {
            *msg_info.mutable_sender() = sender_info;
        } else {
            msg_info.mutable_sender()->set_user_id(from_user_id);
        }
    }

    file::MessageContent* content = msg_info.mutable_message();
    content->set_message_type(convert_message_type(msg_ptr->message_type()));

    signed char msg_type = msg_ptr->message_type();

    if (msg_type == 0) {
        file::StringMessageInfo* str_msg = content->mutable_string_message();
        str_msg->set_content(
            msg_ptr->content().null() ? "" : msg_ptr->content().get());
    } else if (msg_type == 1) {
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

bool MsgStorageServiceImpl::store_file_message(const file::MessageInfo& msg_info) {
    if (!channel_pool_) {
        LOG_ERROR("[MsgStorageServiceImpl] Channel pool not initialized");
        return false;
    }

    const auto& msg_content = msg_info.message();
    std::string file_id;
    std::string file_name;
    int64_t file_size = 0;

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

    if (!file_id.empty()) {
        return true;
    }

    auto channel = channel_pool_->get_channel("file_service");
    if (!channel) {
        LOG_ERROR("[MsgStorageServiceImpl] No file_service channel available");
        return false;
    }

    file::FileService_Stub stub(channel.get());
    file::PutSingleFileReq req;
    file::PutSingleFileRsp rsp;

    req.set_request_id(utils::generate_uuid());
    file::FileUploadData* upload_data = req.mutable_file_data();

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
        return true;
    }

    brpc::Controller cntl;
    stub.PutSingleFile(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed() || !rsp.success()) {
        LOG_ERROR("[MsgStorageServiceImpl] Store file failed: {}",
                  cntl.Failed() ? cntl.ErrorText() : rsp.errmsg());
        return false;
    }

    if (rsp.has_file_info()) {
        LOG_INFO("[MsgStorageServiceImpl] File stored, file_id: {}", rsp.file_info().file_id());
    }

    return true;
}

void MsgStorageServiceImpl::on_message_consume(const std::string& message_str, uint64_t deliveryTag) {
    LOG_INFO("[MsgStorageServiceImpl] Received message from MQ, deliveryTag: {}", deliveryTag);

    file::MessageInfo msg_info;
    if (!msg_info.ParseFromString(message_str)) {
        LOG_ERROR("[MsgStorageServiceImpl] Failed to parse message from MQ");
        mq_client_->reject(deliveryTag, true);
        return;
    }

    LOG_INFO("[MsgStorageServiceImpl] Message parsed, message_id: {}, session: {}",
             msg_info.message_id(), msg_info.chat_session_id());

    if (!message_table_) {
        LOG_ERROR("[MsgStorageServiceImpl] Message table not initialized");
        mq_client_->reject(deliveryTag, true);
        return;
    }

    try {
        msg_record db_msg(msg_info.message_id());
        db_msg.to_session_id(msg_info.chat_session_id());

        std::string time_str = time_util::ms_to_string(msg_info.timestamp());
        db_msg.created_time(time_str);

        if (msg_info.has_sender()) {
            db_msg.from_user_id(msg_info.sender().user_id());
        }

        signed char msg_type = static_cast<signed char>(msg_info.message().message_type());
        db_msg.message_type(msg_type);

        if (msg_type == 0 && msg_info.message().has_string_message()) {
            const auto& str_msg = msg_info.message().string_message();
            db_msg.content(str_msg.content());

            if (message_es_) {
                message_es::ESMessage es_msg;
                es_msg.chat_session_id = msg_info.chat_session_id();
                es_msg.message_id = msg_info.message_id();
                es_msg.content = str_msg.content();
                message_es_->insert_message(es_msg);
            }
        } else if (msg_type == 1 && msg_info.message().has_image_message()) {
            const auto& img_msg = msg_info.message().image_message();
            if (img_msg.has_file_id()) {
                db_msg.file_id(img_msg.file_id());
            }
        } else if (msg_type == 2 && msg_info.message().has_file_message()) {
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
            const auto& speech_msg = msg_info.message().speech_message();
            if (speech_msg.has_file_id()) {
                db_msg.file_id(speech_msg.file_id());
            }
        }

        if (!message_table_->insert(db_msg)) {
            LOG_ERROR("[MsgStorageServiceImpl] Failed to insert message to DB, message_id: {}",
                      msg_info.message_id());
            mq_client_->reject(deliveryTag, true);
            return;
        }

        if (msg_type != 0) {
            bool has_file_data = false;
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

            if (has_file_data) {
                store_file_message(msg_info);
            }
        }

        mq_client_->ack(deliveryTag);
        LOG_INFO("[MsgStorageServiceImpl] Message stored successfully, message_id: {}",
                 msg_info.message_id());

    } catch (const std::exception& e) {
        LOG_ERROR("[MsgStorageServiceImpl] Exception in message consume: {}", e.what());
        mq_client_->reject(deliveryTag, true);
    }
}

void MsgStorageServiceImpl::GetHistoryMsg(google::protobuf::RpcController* cntl_base,
                                           const GetHistoryMsgReq* request,
                                           GetHistoryMsgRsp* response,
                                           google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

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

    std::string start_time_str = time_util::ms_to_string(start_time_ms);
    std::string end_time_str = time_util::ms_to_string(end_time_ms);

    auto messages = message_table_->select_by_time_range(session_id, start_time_str, end_time_str);

    for (const auto& msg_ptr : messages) {
        file::MessageInfo msg_info = construct_message_info(msg_ptr);
        *response->add_msg_list() = msg_info;
    }

    response->set_success(true);
    LOG_INFO("[MsgStorageServiceImpl] GetHistoryMsg success, session: {}, count: {}",
             session_id, response->msg_list_size());
}

void MsgStorageServiceImpl::GetRecentMsg(google::protobuf::RpcController* cntl_base,
                                          const GetRecentMsgReq* request,
                                          GetRecentMsgRsp* response,
                                          google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

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

    std::vector<std::shared_ptr<msg_record>> messages;

    if (request->has_cur_time() && request->cur_time() > 0) {
        std::string before_time_str = time_util::ms_to_string(request->cur_time());
        messages = message_table_->select_recent_before(session_id, count, before_time_str);
    } else {
        messages = message_table_->select_recent(session_id, count);
    }

    for (const auto& msg_ptr : messages) {
        file::MessageInfo msg_info = construct_message_info(msg_ptr);
        *response->add_msg_list() = msg_info;
    }

    response->set_success(true);
    LOG_INFO("[MsgStorageServiceImpl] GetRecentMsg success, session: {}, count: {}",
             session_id, response->msg_list_size());
}

void MsgStorageServiceImpl::MsgSearch(google::protobuf::RpcController* cntl_base,
                                       const MsgSearchReq* request,
                                       MsgSearchRsp* response,
                                       google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    (void)cntl;

    response->set_request_id(request->request_id());
    response->set_success(false);

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

    std::vector<message_es::ESMessage> es_results;
    if (!message_es_->search_by_keyword(session_id, search_key, es_results)) {
        LOG_ERROR("[MsgStorageServiceImpl] ES search failed, session: {}, key: {}",
                  session_id, search_key);
        response->set_errmsg("Search failed");
        return;
    }

    if (es_results.empty()) {
        response->set_success(true);
        LOG_INFO("[MsgStorageServiceImpl] MsgSearch no results, session: {}, key: {}",
                 session_id, search_key);
        return;
    }

    std::vector<std::string> message_ids;
    for (const auto& es_msg : es_results) {
        message_ids.push_back(es_msg.message_id);
    }

    auto db_messages = message_table_->select_by_message_ids(message_ids);

    for (const auto& msg_ptr : db_messages) {
        file::MessageInfo msg_info = construct_message_info(msg_ptr);
        *response->add_msg_list() = msg_info;
    }

    response->set_success(true);
    LOG_INFO("[MsgStorageServiceImpl] MsgSearch success, session: {}, key: {}, es_hits: {}, db_hits: {}",
             session_id, search_key, es_results.size(), response->msg_list_size());
}

} // namespace message