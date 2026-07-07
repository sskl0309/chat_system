// gflags: brpc 使用的命令行参数解析库
#include <gflags/gflags.h>
// brpc 客户端 Channel 头文件，用于建立与服务端的连接
#include <brpc/channel.h>
// 由 main.proto 自动生成的头文件
#include "main.pb.h"

// 命令行参数：服务端地址
DEFINE_string(server_addr, "0.0.0.0:8000", "Address of the echo server");
// 命令行参数：要发送的消息内容
DEFINE_string(message, "hello world", "Message to send to the echo server");

int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::SetUsageMessage("Echo client");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // Channel: brpc 的客户端通信通道，管理与服务端的 TCP 长连接
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";    // 使用 baidu_std 二进制协议（brpc 默认协议）
    options.timeout_ms = 1000;         // 单次 RPC 调用超时时间 1 秒
    options.max_retry = 3;             // 失败后最多重试 3 次

    // 初始化 Channel，连接目标服务端
    if (channel.Init(FLAGS_server_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Failed to initialize channel to " << FLAGS_server_addr;
        return -1;
    }

    // Stub: 客户端存根，通过它来发起 RPC 调用
    echo::EchoService_Stub stub(&channel);

    // Controller: 单次 RPC 调用的控制器，可获取调用状态、错误信息等
    brpc::Controller cntl;
    echo::EchoRequest request;
    echo::EchoResponse response;

    // 设置请求消息
    request.set_message(FLAGS_message);

    LOG(INFO) << "Sending request: " << request.message();
    // 发起同步 RPC 调用，最后一个参数为 NULL 表示同步等待
    stub.Echo(&cntl, &request, &response, NULL);

    // 检查 RPC 是否成功
    if (cntl.Failed()) {
        LOG(ERROR) << "RPC failed: " << cntl.ErrorText();
        return -1;
    }

    LOG(INFO) << "Received response: " << response.message();

    return 0;
}
