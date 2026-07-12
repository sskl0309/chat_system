#include <gflags/gflags.h>
#include <brpc/channel.h>
#include "main.pb.h"
#include "../common/log.hpp"
#include "../common/brpc_client.hpp"

using namespace mylog;

DEFINE_string(etcd_addr, "http://localhost:2379", "etcd server address");
DEFINE_string(service_name, "echo_service", "Service name to call");
DEFINE_int32(call_count, 5, "Number of RPC calls to make");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    mylog::init(true, "client.log");

    LOG_INFO("[Client] Initializing channel pool with etcd: {}", FLAGS_etcd_addr);

    brpc::ServiceChannelPool pool;
    if (!pool.init_with_etcd(FLAGS_etcd_addr)) {
        LOG_ERROR("[Client] Failed to init channel pool with etcd");
        return -1;
    }

    LOG_INFO("[Client] Waiting for service {} to be available...", FLAGS_service_name);

    int wait_count = 0;
    while (!pool.has_service(FLAGS_service_name)) {
        if (wait_count >= 10) {
            LOG_ERROR("[Client] Service {} not available after waiting", FLAGS_service_name);
            return -1;
        }
        LOG_INFO("[Client] Waiting... ({}/10)", wait_count + 1);
        sleep(1);
        wait_count++;
    }

    LOG_INFO("[Client] Service {} is available, starting RPC calls", FLAGS_service_name);

    for (int i = 0; i < FLAGS_call_count; i++) {
        auto channel = pool.get_channel(FLAGS_service_name);
        if (!channel) {
            LOG_ERROR("[Client] Failed to get channel for {}", FLAGS_service_name);
            continue;
        }

        echo::EchoService_Stub stub(channel.get());

        brpc::Controller cntl;
        echo::EchoRequest request;
        echo::EchoResponse response;

        request.set_message("Hello, bRPC! Call " + std::to_string(i + 1));

        cntl.set_timeout_ms(1000);

        stub.Echo(&cntl, &request, &response, nullptr);

        if (cntl.Failed()) {
            LOG_ERROR("[Client] RPC failed: {}", cntl.ErrorText());
        } else {
            LOG_INFO("[Client] Call {}: Received response - {}", i + 1, response.message());
        }

        usleep(500000);
    }

    LOG_INFO("[Client] All {} RPC calls completed", FLAGS_call_count);

    return 0;
}