#include "log.hpp"

void test_function() {
    LOG_TRACE("这是一条trace日志");
    LOG_DEBUG("这是一条debug日志");
    LOG_INFO("这是一条info日志");
    LOG_WARN("这是一条warn日志");
    LOG_ERROR("这是一条error日志");
    LOG_CRITICAL("这是一条critical日志");
}

int main() {
    mylog::init(true);
    LOG_INFO("=== 注释版测试 ===");
    test_function();
    return 0;
}
