// gflags: brpc 使用的命令行参数解析库
#include <gflags/gflags.h>
// brpc 服务端头文件
#include <brpc/server.h>
// 由 main.proto 自动生成的头文件
#include "main.pb.h"

// 命令行参数：服务端监听端口
DEFINE_int32(port, 8000, "TCP port of this server");
// 命令行参数：监听地址，为空时默认 0.0.0.0
DEFINE_string(listen_addr, "", "Server listen address, default is 0.0.0.0");

namespace echo {

// EchoService 的服务端实现类，继承自 proto 自动生成的基类
class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {}
    virtual ~EchoServiceImpl() {}

    // Echo RPC 的实际业务逻辑
    // 参数说明：
    //   cntl_base - RPC 控制器，可获取连接信息、设置响应头等
    //   request   - 客户端发来的请求
    //   response  - 将要返回给客户端的响应
    //   done      - 回调闭包，处理完毕后必须调用 done->Run()
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        // ClosureGuard: RAII 守卫，析构时自动调用 done->Run()，确保回调一定被执行
        brpc::ClosureGuard done_guard(done);

        // 将 RpcController 转换为 brpc 的 Controller，以获取更多信息
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        // 设置 HTTP 响应的 Content-Type（通过浏览器访问时生效）
        cntl->http_response().set_content_type("text/plain");

        LOG(INFO) << "Received request: " << request->message()
                  << " (from " << cntl->remote_side() << ")";

        // 回显：将请求消息原样填入响应
        response->set_message(request->message());

        LOG(INFO) << "Sent response: " << response->message();
    }
};

} // namespace echo

int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::SetUsageMessage("Echo server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 创建 brpc 服务器实例
    brpc::Server server;

    // 创建服务实现对象
    echo::EchoServiceImpl echo_service;

    // 将服务注册到服务器
    // SERVER_DOESNT_OWN_SERVICE 表示 server 不负责释放 service 对象
    if (server.AddService(&echo_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add echo service";
        return -1;
    }

    // 构建监听地址：listen_addr 为空则使用 0.0.0.0:port
    std::string addr = FLAGS_listen_addr.empty()
                       ? std::string("0.0.0.0:") + std::to_string(FLAGS_port)
                       : FLAGS_listen_addr;

    // 服务器配置
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;  // 不启用空闲连接超时

    // 启动服务器
    if (server.Start(addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Failed to start server on " << addr;
        return -1;
    }

    LOG(INFO) << "Echo server is listening on " << addr;
    // 阻塞等待直到收到退出信号（Ctrl+C）
    server.RunUntilAskedToQuit();

    return 0;
}
