/**
 * @file test_etcd_client.cpp
 * @brief etcd_client.h 功能测试文件
 * 
 * 本测试文件全面测试 etcd 服务注册与发现客户端的各项功能：
 * 1. 服务注册客户端（ServiceRegisterClient）功能测试
 * 2. 服务发现客户端（ServiceDiscoverClient）功能测试
 * 3. 服务监控（Watch）功能测试
 * 4. 服务上下线回调测试
 * 5. 多服务注册与发现测试
 * 
 * 使用说明：
 * 1. 确保 etcd 服务已启动并监听在 localhost:2379
 * 2. 编译：mkdir -p build && cd build && cmake .. && make
 * 3. 运行：./test_etcd_client
 */

#include "etcd_client.h"
#include "../common/log.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>

/**
 * @brief 测试服务注册功能
 * 
 * 测试 ServiceRegisterClient 的注册服务功能，验证服务是否能成功注册到 etcd。
 * 
 * @param etcd_address etcd 服务器地址
 * @param service_name 服务名称
 * @param host_address 主机地址
 * @return 注册成功返回 true，失败返回 false
 */
bool test_service_register(const std::string& etcd_address, 
                          const std::string& service_name, 
                          const std::string& host_address) {
    LOG_INFO("=== 开始测试: 服务注册 ===");
    
    try {
        // 创建服务注册客户端，Lease 租约设置为 10 秒
        etcd::ServiceRegisterClient register_client(etcd_address, 10);
        
        // 注册服务
        bool success = register_client.register_service(service_name, host_address);
        
        if (success) {
            LOG_INFO("[测试通过] 服务注册成功: {} -> {}", service_name, host_address);
            return true;
        } else {
            LOG_ERROR("[测试失败] 服务注册失败: {} -> {}", service_name, host_address);
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[测试异常] 服务注册过程中发生异常: {}", e.what());
        return false;
    }
}

/**
 * @brief 测试服务发现功能
 * 
 * 测试 ServiceDiscoverClient 的 discover_services() 方法，
 * 验证是否能正确获取 etcd 中注册的所有服务。
 * 
 * @param etcd_address etcd 服务器地址
 * @param root_dir 根目录
 * @return 发现服务成功返回服务数量，失败返回 -1
 */
int test_service_discover(const std::string& etcd_address, const std::string& root_dir) {
    LOG_INFO("=== 开始测试: 服务发现 ===");
    
    try {
        // 创建服务发现客户端
        etcd::ServiceDiscoverClient discover_client(etcd_address);
        // 设置根目录
        discover_client.set_root_directory(root_dir);
        
        // 发现所有服务
        auto services = discover_client.discover_services();
        
        LOG_INFO("[测试结果] 发现 {} 个服务", services.size());
        
        // 输出发现的服务列表
        for (const auto& service : services) {
            LOG_INFO("  服务名称: {}, 主机地址: {}", service.first, service.second);
        }
        
        return services.size();
    } catch (const std::exception& e) {
        LOG_ERROR("[测试异常] 服务发现过程中发生异常: {}", e.what());
        return -1;
    }
}

/**
 * @brief 测试服务监控（Watch）功能
 * 
 * 测试 ServiceDiscoverClient 的 start_watch() 方法，
 * 验证服务上下线事件是否能正确触发回调函数。
 * 
 * @param etcd_address etcd 服务器地址
 * @param root_dir 根目录
 * @return 监控启动成功返回 true，失败返回 false
 */
bool test_service_watch(const std::string& etcd_address, const std::string& root_dir) {
    LOG_INFO("=== 开始测试: 服务监控 ===");
    
    try {
        // 创建服务发现客户端
        etcd::ServiceDiscoverClient discover_client(etcd_address);
        discover_client.set_root_directory(root_dir);
        
        // 原子变量用于同步回调触发状态
        std::atomic<bool> online_triggered(false);
        std::atomic<bool> offline_triggered(false);
        
        // 设置服务上线回调
        discover_client.set_online_callback([&online_triggered](const std::string& service_name, const std::string& host_address) {
            online_triggered = true;
            LOG_INFO("[测试回调] 服务上线事件触发: {} -> {}", service_name, host_address);
        });
        
        // 设置服务下线回调
        discover_client.set_offline_callback([&offline_triggered](const std::string& service_name, const std::string& host_address) {
            offline_triggered = true;
            LOG_INFO("[测试回调] 服务下线事件触发: {} -> {}", service_name, host_address);
        });
        
        // 启动监控
        discover_client.start_watch();
        LOG_INFO("[测试信息] 服务监控已启动");
        
        // 创建一个服务注册客户端，注册一个临时服务
        etcd::ServiceRegisterClient register_client(etcd_address, 5);
        bool register_success = register_client.register_service("watch_test_service", "10.0.0.100:9999");
        
        if (!register_success) {
            LOG_ERROR("[测试失败] 注册测试服务失败");
            discover_client.stop();
            return false;
        }
        
        // 等待一段时间，让 Watch 事件触发
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 检查上线回调是否触发
        if (online_triggered) {
            LOG_INFO("[测试通过] 服务上线回调已触发");
        } else {
            LOG_WARN("[测试警告] 服务上线回调未触发");
        }
        
        // 停止服务注册，等待 Lease 过期触发下线事件
        register_client.stop();
        LOG_INFO("[测试信息] 已停止服务注册，等待 Lease 过期...");
        
        // 等待 Lease 过期（5秒租约 + 额外等待时间）
        std::this_thread::sleep_for(std::chrono::seconds(7));
        
        // 检查下线回调是否触发
        if (offline_triggered) {
            LOG_INFO("[测试通过] 服务下线回调已触发");
        } else {
            LOG_WARN("[测试警告] 服务下线回调未触发");
        }
        
        // 停止监控
        discover_client.stop();
        LOG_INFO("[测试信息] 服务监控已停止");
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[测试异常] 服务监控过程中发生异常: {}", e.what());
        return false;
    }
}

/**
 * @brief 测试多服务注册与发现
 * 
 * 同时注册多个服务，然后验证服务发现是否能正确获取所有服务。
 * 
 * @param etcd_address etcd 服务器地址
 * @return 测试成功返回 true，失败返回 false
 */
bool test_multi_service(const std::string& etcd_address) {
    LOG_INFO("=== 开始测试: 多服务注册与发现 ===");
    
    try {
        const int service_count = 3;
        const std::string service_name_prefix = "multi_service";
        
        // 注册多个服务
        LOG_INFO("[测试信息] 注册 {} 个测试服务", service_count);
        
        for (int i = 1; i <= service_count; ++i) {
            etcd::ServiceRegisterClient register_client(etcd_address, 15);
            std::string service_name = service_name_prefix + "_" + std::to_string(i);
            std::string host_address = "192.168.1." + std::to_string(i) + ":808" + std::to_string(i);
            
            bool success = register_client.register_service(service_name, host_address);
            if (success) {
                LOG_INFO("  已注册服务: {} -> {}", service_name, host_address);
            } else {
                LOG_ERROR("  注册服务失败: {}", service_name);
                return false;
            }
        }
        
        // 等待服务注册完成
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 发现所有服务
        etcd::ServiceDiscoverClient discover_client(etcd_address);
        discover_client.set_root_directory("/services");
        auto services = discover_client.discover_services();
        
        LOG_INFO("[测试结果] 发现 {} 个服务", services.size());
        
        // 验证服务数量
        if (services.size() >= service_count) {
            LOG_INFO("[测试通过] 多服务注册与发现功能正常");
            return true;
        } else {
            LOG_ERROR("[测试失败] 期望发现至少 {} 个服务，实际发现 {} 个", service_count, services.size());
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[测试异常] 多服务测试过程中发生异常: {}", e.what());
        return false;
    }
}

/**
 * @brief 主测试函数
 * 
 * 按顺序执行所有测试用例，输出测试结果汇总。
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数
 * @return 测试全部通过返回 0，有失败返回 1
 */
int main(int argc, char* argv[]) {
    // 初始化日志系统：调试模式，输出到控制台，日志级别为 DEBUG
    mylog::init(true, "etcd_client_test.log", mylog::LogLevel::DEBUG);
    
    // etcd 服务器地址，默认使用本地地址
    std::string etcd_address = "http://localhost:2379";
    
    // 如果命令行提供了 etcd 地址参数，则使用命令行参数
    if (argc > 1) {
        etcd_address = argv[1];
        LOG_INFO("[测试配置] 使用命令行指定的 etcd 地址: {}", etcd_address);
    } else {
        LOG_INFO("[测试配置] 使用默认 etcd 地址: {}", etcd_address);
    }
    
    // 测试结果统计
    int passed_tests = 0;
    int total_tests = 0;
    
    LOG_INFO("==========================================");
    LOG_INFO("  etcd_client.h 功能测试开始");
    LOG_INFO("==========================================");
    
    // 测试1: 服务注册
    LOG_INFO("");
    total_tests++;
    if (test_service_register(etcd_address, "user_service", "192.168.1.100:8080")) {
        passed_tests++;
    }
    
    // 等待服务注册生效
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 测试2: 服务发现
    LOG_INFO("");
    total_tests++;
    int discovered_count = test_service_discover(etcd_address, "/services");
    if (discovered_count >= 0) {
        passed_tests++;
    }
    
    // 测试3: 服务监控
    LOG_INFO("");
    total_tests++;
    if (test_service_watch(etcd_address, "/services")) {
        passed_tests++;
    }
    
    // 测试4: 多服务注册与发现
    LOG_INFO("");
    total_tests++;
    if (test_multi_service(etcd_address)) {
        passed_tests++;
    }
    
    // 输出测试结果汇总
    LOG_INFO("");
    LOG_INFO("==========================================");
    LOG_INFO("  etcd_client.h 功能测试结束");
    LOG_INFO("==========================================");
    LOG_INFO("[测试结果] 总计: {} 个测试，通过: {} 个，失败: {} 个", 
             total_tests, passed_tests, total_tests - passed_tests);
    
    if (passed_tests == total_tests) {
        LOG_INFO("[测试总结] 所有测试用例均已通过！");
        return 0;
    } else {
        LOG_ERROR("[测试总结] 部分测试用例失败，请检查 etcd 服务状态和网络连接");
        return 1;
    }
}