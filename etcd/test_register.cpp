/**
 * @file test_register.cpp
 * @brief 服务注册客户端测试
 * 
 * 单独测试 ServiceRegisterClient 的功能，向 etcd 注册服务并保持活跃。
 * 使用方法：先运行 test_discover.cpp，再运行此文件，观察发现客户端的回调触发。
 */

#include "../common/etcd_client.h"
#include "../common/log.hpp"
#include <thread>
#include <chrono>

int main() {
    mylog::init(true, "register_test.log", mylog::LogLevel::INFO);
    
    std::string etcd_address = "http://localhost:2379";
    
    LOG_INFO("========================================");
    LOG_INFO("  服务注册客户端测试");
    LOG_INFO("========================================");
    LOG_INFO("[测试信息] etcd 地址: {}", etcd_address);
    
    etcd::ServiceRegisterClient register_client(etcd_address, 30);
    
    std::string service_name = "payment_service";
    std::string host_address = "192.168.1.100:8080";
    
    LOG_INFO("[测试步骤] 注册服务: {} -> {}", service_name, host_address);
    
    bool success = register_client.register_service(service_name, host_address);
    
    if (success) {
        LOG_INFO("[测试成功] 服务注册成功！");
        LOG_INFO("[测试信息] 服务将保持活跃，按 Ctrl+C 退出");
        
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            LOG_INFO("[测试信息] 服务保活中...");
        }
    } else {
        LOG_ERROR("[测试失败] 服务注册失败");
        return 1;
    }
    
    return 0;
}