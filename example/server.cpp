#include <gflags/gflags.h>
#include <brpc/server.h>
#include "main.pb.h"
#include "../common/log.hpp"
#include "../common/etcd_client.hpp"

using namespace mylog;

DEFINE_int32(port, 8000, "TCP port of this server");
DEFINE_string(listen_addr, "", "Server listen address, default is 0.0.0.0");
DEFINE_string(etcd_addr, "http://localhost:2379", "etcd server address");

namespace echo {

class EchoServiceImpl : public EchoService {
public:
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->http_response().set_content_type("text/plain");

        LOG_INFO("[Server] Received request: {}", request->message());

        response->set_message(request->message());

        LOG_INFO("[Server] Sent response: {}", response->message());
    }
};

}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    mylog::init(true, "server.log");

    std::string service_name = "echo_service";
    std::string host_address = "127.0.0.1:" + std::to_string(FLAGS_port);

    LOG_INFO("[Server] Starting {} on {}", service_name, host_address);

    etcd::ServiceRegisterClient register_client(FLAGS_etcd_addr);
    if (!register_client.register_service(service_name, host_address)) {
        LOG_ERROR("[Server] Failed to register service to etcd");
        return -1;
    }

    brpc::Server server;
    echo::EchoServiceImpl echo_service;

    if (server.AddService(&echo_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR("[Server] Failed to add echo service");
        return -1;
    }

    std::string addr = FLAGS_listen_addr.empty()
                       ? std::string("0.0.0.0:") + std::to_string(FLAGS_port)
                       : FLAGS_listen_addr;

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;

    if (server.Start(addr.c_str(), &options) != 0) {
        LOG_ERROR("[Server] Failed to start server on {}", addr);
        return -1;
    }

    LOG_INFO("[Server] Echo server is listening on {}", addr);
    LOG_INFO("[Server] Service registered to etcd: {} -> {}", service_name, host_address);

    server.RunUntilAskedToQuit();

    return 0;
}