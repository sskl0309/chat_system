/**
 * @file test_discover.cpp
 * @brief 服务发现客户端测试
 * 
 * 单独测试 ServiceDiscoverClient 的功能，监听 etcd 中服务的变化。
 * 使用方法：先运行此文件，再运行 test_register.cpp，观察服务上线回调的触发。
 */

#include "../common/etcd_client.h"
#include "../common/log.hpp"
#include <thread>
#include <chrono>

int main() {
    mylog::init(true, "discover_test.log", mylog::LogLevel::INFO);
    
    std::string etcd_address = "http://localhost:2379";
    
    LOG_INFO("========================================");
    LOG_INFO("  服务发现客户端测试");
    LOG_INFO("========================================");
    LOG_INFO("[测试信息] etcd 地址: {}", etcd_address);
    
    etcd::ServiceDiscoverClient discover_client(etcd_address);
    discover_client.set_root_directory("/services");
    
    discover_client.set_online_callback([](const std::string& service_name, const std::string& host_address) {
        LOG_INFO("\n****************************************");
        LOG_INFO("*  [上线回调触发] 服务上线！");
        LOG_INFO("*  服务名称: {}", service_name);
        LOG_INFO("*  主机地址: {}", host_address);
        LOG_INFO("****************************************\n");
    });
    
    discover_client.set_offline_callback([](const std::string& service_name, const std::string& host_address) {
        LOG_INFO("\n****************************************");
        LOG_INFO("*  [下线回调触发] 服务下线！");
        LOG_INFO("*  服务名称: {}", service_name);
        LOG_INFO("*  主机地址: {}", host_address);
        LOG_INFO("****************************************\n");
    });
    
    LOG_INFO("[测试步骤] 启动服务监控...");
    discover_client.start_watch();
    
    LOG_INFO("[测试步骤] 发现当前所有服务...");
    auto services = discover_client.discover_services();
    LOG_INFO("[测试结果] 当前发现 {} 个服务:", services.size());
    for (const auto& service : services) {
        LOG_INFO("  {} -> {}", service.first, service.second);
    }
    
    LOG_INFO("[测试信息] 等待服务变化...");
    LOG_INFO("[测试信息] 请启动 test_register.cpp 来触发上线回调");
    LOG_INFO("[测试信息] 按 Ctrl+C 退出\n");
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        LOG_INFO("[测试信息] 监控中...");
    }
    
    return 0;
}