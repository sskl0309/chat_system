// =============================================================================
// file_client_test.cc - 文件服务 gtest 测试客户端
// =============================================================================
// 基于 Google Test 框架测试文件子服务的全部 4 个 RPC 接口：
//   1. PutSingleFile  - 单文件上传
//   2. PutMultiFile   - 多文件批量上传
//   3. GetSingleFile  - 单文件下载
//   4. GetMultiFile   - 多文件批量下载
//
// 测试策略：
//   - 使用 Test Fixture 管理 etcd 服务发现与 brpc Channel 生命周期
//   - 上传类测试验证返回的 file_id / file_info 正确性
//   - 下载类测试采用 round-trip 方式：先上传获取 file_id，再下载验证内容一致性
//   - 通过 ServiceChannelPool 自动发现 file_service 实例
//
// 运行前提：
//   - 需要已启动的 file_server 实例注册到 etcd
//   - 可通过 --test_etcd_addr 指定 etcd 地址
//
// 运行方式：
//   ./file_client_test --test_etcd_addr=127.0.0.1:2379
// =============================================================================

#include <gtest/gtest.h>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <ctime>

#include "brpc_client.hpp"
#include "utils.hpp"
#include "log.hpp"

// 文件服务 protobuf 定义
#include "../build/file.pb.h"

// ==================== gflags 命令行参数定义 ====================

DEFINE_string(test_etcd_addr, "127.0.0.1:2379", "Etcd server address for tests");
DEFINE_string(test_service_name, "file_service", "Target service name for discovery");
DEFINE_int32(test_timeout_ms, 10000, "RPC timeout in milliseconds");

// ==================== 测试夹具 ====================

/**
 * @brief 文件服务测试夹具
 *
 * 管理测试生命周期：服务发现初始化、测试文件创建与清理、信道获取。
 * 所有测试用例共享同一个 ServiceChannelPool 实例，避免重复初始化 etcd 连接。
 */
class FileServiceTest : public ::testing::Test {
protected:
    // ---------- 测试文件路径与内容 ----------
    static constexpr const char* kTestFile1 = "/tmp/gtest_file_1.txt";
    static constexpr const char* kTestFile2 = "/tmp/gtest_file_2.txt";
    static constexpr const char* kTestFile3 = "/tmp/gtest_file_3.txt";

    const std::string kContent1 = "Hello, this is test file 1 content!\nLine 2 of file 1.";
    const std::string kContent2 = "This is test file 2 with different content.\nSecond line here.";
    const std::string kContent3 = "Third test file content. Short and simple.";

    // ---------- 服务发现与信道 ----------
    std::unique_ptr<brpc::ServiceChannelPool> pool_;
    brpc::ChannelOptions options_;
    std::shared_ptr<::brpc::Channel> channel_;  // 保持信道存活

    /**
     * @brief 测试前置：初始化日志、创建测试文件、连接 etcd
     */
    void SetUp() override {
        // 初始化日志（debug 模式输出到控制台）
        mylog::init(true, "", mylog::LogLevel::INFO);

        // 固定随机种子，保证 request_id 可复现
        std::srand(static_cast<unsigned>(std::time(nullptr)));

        // 创建测试用临时文件
        ASSERT_TRUE(utils::writeFile(kTestFile1, kContent1))
            << "Failed to create test file: " << kTestFile1;
        ASSERT_TRUE(utils::writeFile(kTestFile2, kContent2))
            << "Failed to create test file: " << kTestFile2;
        ASSERT_TRUE(utils::writeFile(kTestFile3, kContent3))
            << "Failed to create test file: " << kTestFile3;

        // 配置 brpc Channel 选项
        options_.protocol = brpc::PROTOCOL_BAIDU_STD;
        options_.connection_type = brpc::CONNECTION_TYPE_SHORT;
        options_.timeout_ms = FLAGS_test_timeout_ms;
        options_.max_retry = 3;

        // 初始化 ServiceChannelPool，连接 etcd 进行服务发现
        std::string etcd_url = "http://" + FLAGS_test_etcd_addr;
        pool_ = std::make_unique<brpc::ServiceChannelPool>();
        pool_->set_default_channel_options(options_);
        pool_->init_with_etcd(etcd_url, "/services");

        // 等待服务发现（最多等待 10 秒）
        int wait_count = 0;
        while (!pool_->has_service(FLAGS_test_service_name) && wait_count < 10) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            wait_count++;
        }

        ASSERT_TRUE(pool_->has_service(FLAGS_test_service_name))
            << "Service discovery timeout for: " << FLAGS_test_service_name;
    }

    /**
     * @brief 测试后置：清理测试文件
     */
    void TearDown() override {
        std::remove(kTestFile1);
        std::remove(kTestFile2);
        std::remove(kTestFile3);
    }

    // ---------- 辅助方法 ----------

    /**
     * @brief 刷新信道并创建 RPC Stub
     *
     * 从 ServiceChannelPool 获取一个信道（RR 轮询），
     * 构造对应服务的 FileService_Stub。
     *
     * @return file::FileService_Stub 实例
     */
    file::FileService_Stub create_stub() {
        channel_ = pool_->get_channel(FLAGS_test_service_name);
        EXPECT_NE(channel_, nullptr) << "Failed to get channel from pool";
        return file::FileService_Stub(channel_.get());
    }

    /**
     * @brief 生成测试用 request_id
     */
    static std::string gen_request_id(const std::string& prefix) {
        return prefix + "_" + std::to_string(std::rand() % 100000);
    }

    /**
     * @brief 上传单个文件并返回 file_id
     *
     * 辅助方法，用于下载类测试中准备测试数据。
     *
     * @param file_path 本地文件路径
     * @return std::string 上传成功后服务端返回的 file_id，失败返回空字符串
     */
    std::string upload_single_file(const std::string& file_path) {
        auto stub = create_stub();

        std::string content;
        if (!utils::readFile(file_path, content)) {
            return "";
        }

        file::PutSingleFileReq req;
        file::PutSingleFileRsp rsp;
        brpc::Controller cntl;

        req.set_request_id(gen_request_id("upload"));
        req.set_user_id("test_user");

        auto* data = req.mutable_file_data();
        data->set_file_name(file_path.substr(file_path.find_last_of("/") + 1));
        data->set_file_size(static_cast<int64_t>(content.size()));
        data->set_file_content(content);

        stub.PutSingleFile(&cntl, &req, &rsp, nullptr);

        if (cntl.Failed() || !rsp.success()) {
            return "";
        }
        return rsp.file_info().file_id();
    }

    /**
     * @brief 从文件路径提取文件名
     */
    static std::string extract_file_name(const std::string& file_path) {
        size_t pos = file_path.find_last_of("/");
        if (pos == std::string::npos) {
            return file_path;
        }
        return file_path.substr(pos + 1);
    }
};

// ==================== 测试用例 ====================

// ---------------------------------------------------------------------------
// 测试 1：PutSingleFile - 单文件上传
// ---------------------------------------------------------------------------
TEST_F(FileServiceTest, PutSingleFile) {
    auto stub = create_stub();

    std::string content;
    ASSERT_TRUE(utils::readFile(kTestFile1, content));

    file::PutSingleFileReq req;
    file::PutSingleFileRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(gen_request_id("put_single"));
    req.set_user_id("test_user");
    auto* data = req.mutable_file_data();
    data->set_file_name(extract_file_name(kTestFile1));
    data->set_file_size(static_cast<int64_t>(content.size()));
    data->set_file_content(content);

    stub.PutSingleFile(&cntl, &req, &rsp, nullptr);

    // 验证 RPC 调用成功
    ASSERT_FALSE(cntl.Failed()) << "RPC error: " << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << "Server error: " << rsp.errmsg();

    // 验证返回的文件元信息
    const auto& info = rsp.file_info();
    EXPECT_FALSE(info.file_id().empty()) << "file_id should not be empty";
    EXPECT_EQ(info.file_name(), extract_file_name(kTestFile1));
    EXPECT_EQ(info.file_size(), static_cast<int64_t>(content.size()));

    LOG_INFO("[PutSingleFile] PASS - file_id={}, latency={}us",
             info.file_id(), cntl.latency_us());
}

// ---------------------------------------------------------------------------
// 测试 2：GetSingleFile - 单文件下载（round-trip）
// ---------------------------------------------------------------------------
TEST_F(FileServiceTest, GetSingleFile) {
    // Step 1: 先上传文件，获取 file_id
    std::string file_id = upload_single_file(kTestFile1);
    ASSERT_FALSE(file_id.empty()) << "Upload failed, cannot proceed with download test";

    // Step 2: 通过 file_id 下载文件
    auto stub = create_stub();

    file::GetSingleFileReq req;
    file::GetSingleFileRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(gen_request_id("get_single"));
    req.set_file_id(file_id);

    stub.GetSingleFile(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << "RPC error: " << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << "Server error: " << rsp.errmsg();
    ASSERT_TRUE(rsp.has_file_data()) << "Response missing file_data";

    // 验证下载内容与上传内容一致
    EXPECT_EQ(rsp.file_data().file_id(), file_id);
    EXPECT_EQ(rsp.file_data().file_content(), kContent1);

    LOG_INFO("[GetSingleFile] PASS - file_id={}, size={}, latency={}us",
             file_id, rsp.file_data().file_content().size(), cntl.latency_us());
}

// ---------------------------------------------------------------------------
// 测试 3：PutMultiFile - 多文件批量上传
// ---------------------------------------------------------------------------
TEST_F(FileServiceTest, PutMultiFile) {
    auto stub = create_stub();

    std::vector<std::pair<std::string, std::string>> files = {
        {kTestFile1, kContent1},
        {kTestFile2, kContent2},
        {kTestFile3, kContent3},
    };

    file::PutMultiFileReq req;
    file::PutMultiFileRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(gen_request_id("put_multi"));
    req.set_user_id("test_user");

    for (const auto& f : files) {
        auto* data = req.add_file_data();
        data->set_file_name(extract_file_name(f.first));
        data->set_file_size(static_cast<int64_t>(f.second.size()));
        data->set_file_content(f.second);
    }

    stub.PutMultiFile(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << "RPC error: " << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << "Server error: " << rsp.errmsg();
    EXPECT_EQ(rsp.file_info_size(), static_cast<int>(files.size()));

    // 验证每个文件都返回了有效的 file_id
    for (int i = 0; i < rsp.file_info_size(); ++i) {
        const auto& info = rsp.file_info(i);
        EXPECT_FALSE(info.file_id().empty())
            << "file_info[" << i << "] file_id is empty";
        EXPECT_EQ(info.file_name(), extract_file_name(files[i].first))
            << "file_info[" << i << "] file_name mismatch";
    }

    LOG_INFO("[PutMultiFile] PASS - count={}, latency={}us",
             rsp.file_info_size(), cntl.latency_us());
}

// ---------------------------------------------------------------------------
// 测试 4：GetMultiFile - 多文件批量下载（round-trip）
// ---------------------------------------------------------------------------
TEST_F(FileServiceTest, GetMultiFile) {
    // Step 1: 上传多个文件，收集 file_id
    std::vector<std::string> file_ids;
    std::vector<std::string> contents = {kContent1, kContent2};
    std::vector<const char*> paths = {kTestFile1, kTestFile2};

    for (size_t i = 0; i < paths.size(); ++i) {
        std::string id = upload_single_file(paths[i]);
        ASSERT_FALSE(id.empty()) << "Upload failed for file: " << paths[i];
        file_ids.push_back(id);
    }

    // Step 2: 通过 file_id 列表批量下载
    auto stub = create_stub();

    file::GetMultiFileReq req;
    file::GetMultiFileRsp rsp;
    brpc::Controller cntl;

    req.set_request_id(gen_request_id("get_multi"));
    for (const auto& id : file_ids) {
        req.add_file_id_list(id);
    }

    stub.GetMultiFile(&cntl, &req, &rsp, nullptr);

    ASSERT_FALSE(cntl.Failed()) << "RPC error: " << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << "Server error: " << rsp.errmsg();
    EXPECT_EQ(rsp.file_data_size(), static_cast<int>(file_ids.size()));

    // 验证每个文件的下载内容与上传内容一致
    for (size_t i = 0; i < file_ids.size(); ++i) {
        const auto& id = file_ids[i];
        ASSERT_TRUE(rsp.file_data().contains(id))
            << "Response missing file_id: " << id;
        EXPECT_EQ(rsp.file_data().at(id).file_content(), contents[i])
            << "Content mismatch for file_id: " << id;
    }

    LOG_INFO("[GetMultiFile] PASS - count={}, latency={}us",
             rsp.file_data_size(), cntl.latency_us());
}

// ---------------------------------------------------------------------------
// 测试 5：FullRoundTrip - 完整往返测试（上传→下载→校验）
// ---------------------------------------------------------------------------
TEST_F(FileServiceTest, FullRoundTrip) {
    // 上传三个文件
    std::vector<std::string> file_ids;
    std::vector<std::string> contents = {kContent1, kContent2, kContent3};
    std::vector<const char*> paths = {kTestFile1, kTestFile2, kTestFile3};

    for (size_t i = 0; i < paths.size(); ++i) {
        std::string id = upload_single_file(paths[i]);
        ASSERT_FALSE(id.empty()) << "Upload failed for: " << paths[i];
        file_ids.push_back(id);
        LOG_INFO("[FullRoundTrip] Uploaded: {} -> {}", paths[i], id);
    }

    // 逐个下载并校验
    auto stub = create_stub();
    for (size_t i = 0; i < file_ids.size(); ++i) {
        file::GetSingleFileReq req;
        file::GetSingleFileRsp rsp;
        brpc::Controller cntl;

        req.set_request_id(gen_request_id("roundtrip"));
        req.set_file_id(file_ids[i]);

        stub.GetSingleFile(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed())
            << "RPC error for " << file_ids[i] << ": " << cntl.ErrorText();
        ASSERT_TRUE(rsp.success())
            << "Server error for " << file_ids[i] << ": " << rsp.errmsg();
        ASSERT_TRUE(rsp.has_file_data());

        EXPECT_EQ(rsp.file_data().file_content(), contents[i])
            << "Round-trip content mismatch for: " << file_ids[i];
    }

    // 批量下载并校验
    {
        file::GetMultiFileReq req;
        file::GetMultiFileRsp rsp;
        brpc::Controller cntl;

        req.set_request_id(gen_request_id("roundtrip_multi"));
        for (const auto& id : file_ids) {
            req.add_file_id_list(id);
        }

        stub.GetMultiFile(&cntl, &req, &rsp, nullptr);

        ASSERT_FALSE(cntl.Failed()) << "Multi-download RPC error: " << cntl.ErrorText();
        ASSERT_TRUE(rsp.success()) << "Multi-download server error: " << rsp.errmsg();
        EXPECT_EQ(rsp.file_data_size(), static_cast<int>(file_ids.size()));

        for (size_t i = 0; i < file_ids.size(); ++i) {
            ASSERT_TRUE(rsp.file_data().contains(file_ids[i]));
            EXPECT_EQ(rsp.file_data().at(file_ids[i]).file_content(), contents[i]);
        }
    }

    LOG_INFO("[FullRoundTrip] PASS - all 3 files uploaded and verified");
}

// ==================== main ====================

int main(int argc, char* argv[]) {
    // 解析 gflags 和 gtest 参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
