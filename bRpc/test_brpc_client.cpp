#include "../common/brpc_client.hpp"

int main() {
    brpc::ServiceChannelPool pool;
    pool.add_service_channel("test_service", "127.0.0.1:8000");
    auto channel = pool.get_channel("test_service");
    if (channel) {
        return 0;
    }
    return -1;
}
