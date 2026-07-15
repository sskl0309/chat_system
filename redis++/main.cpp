/**
 * @file main.cpp
 * @brief redis-plus-plus 简单使用示例
 *
 * 演示 redis-plus-plus 的基本操作：
 * 1. 连接 Redis
 * 2. String 操作 (set/get/del)
 * 3. Hash 操作 (hset/hget/hgetall)
 * 4. List 操作 (lpush/lrange)
 * 5. Set 操作 (sadd/smembers)
 *
 * 使用说明：
 * 1. 确保 Redis 服务已启动并监听在 localhost:6379
 * 2. 编译：mkdir -p build && cd build && cmake .. && make
 * 3. 运行：./redis_example
 */

#include <sw/redis++/redis++.h>
#include <iostream>
#include <vector>
#include <unordered_map>

using namespace sw::redis;

int main() {
    try {
        // 1. 创建 Redis 连接
        auto redis = Redis("tcp://127.0.0.1:6379");

        // 测试连接
        std::cout << "Redis 连接成功，PING 响应: " << redis.ping() << std::endl;

        // ==================== String 操作 ====================
        std::cout << "\n=== String 操作 ===" << std::endl;

        // SET
        redis.set("name", "Alice");
        redis.set("age", "25");
        std::cout << "SET name=Alice, age=25" << std::endl;

        // GET
        auto name = redis.get("name");
        auto age = redis.get("age");
        std::cout << "GET name: " << (name ? *name : "nil") << std::endl;
        std::cout << "GET age: " << (age ? *age : "nil") << std::endl;

        // SET 带过期时间（10秒）
        redis.set("temp_key", "expire_value", std::chrono::seconds(10));
        std::cout << "SET temp_key 带 10 秒过期时间" << std::endl;

        // DEL
        redis.del("age");
        std::cout << "DEL age" << std::endl;

        // ==================== Hash 操作 ====================
        std::cout << "\n=== Hash 操作 ===" << std::endl;

        // HSET
        redis.hset("user:1001", "name", "Bob");
        redis.hset("user:1001", "email", "bob@example.com");
        redis.hset("user:1001", "score", "95");
        std::cout << "HSET user:1001 name=Bob email=bob@example.com score=95" << std::endl;

        // HGET
        auto hname = redis.hget("user:1001", "name");
        std::cout << "HGET user:1001 name: " << (hname ? *hname : "nil") << std::endl;

        // HGETALL
        std::unordered_map<std::string, std::string> user_info;
        redis.hgetall("user:1001", std::inserter(user_info, user_info.begin()));
        std::cout << "HGETALL user:1001:" << std::endl;
        for (const auto& [k, v] : user_info) {
            std::cout << "  " << k << ": " << v << std::endl;
        }

        // ==================== List 操作 ====================
        std::cout << "\n=== List 操作 ===" << std::endl;

        // LPUSH
        redis.del("tasks");  // 先清空
        redis.lpush("tasks", {"task3", "task2", "task1"});
        std::cout << "LPUSH tasks task1 task2 task3" << std::endl;

        // RPUSH
        redis.rpush("tasks", "task4");
        std::cout << "RPUSH tasks task4" << std::endl;

        // LRANGE
        std::vector<std::string> tasks;
        redis.lrange("tasks", 0, -1, std::back_inserter(tasks));
        std::cout << "LRANGE tasks 0 -1:" << std::endl;
        for (const auto& t : tasks) {
            std::cout << "  " << t << std::endl;
        }

        // ==================== Set 操作 ====================
        std::cout << "\n=== Set 操作 ===" << std::endl;

        // SADD
        redis.del("tags");  // 先清空
        redis.sadd("tags", {"c++", "redis", "linux"});
        std::cout << "SADD tags c++ redis linux" << std::endl;

        // SMEMBERS
        std::vector<std::string> tags;
        redis.smembers("tags", std::back_inserter(tags));
        std::cout << "SMEMBERS tags:" << std::endl;
        for (const auto& t : tags) {
            std::cout << "  " << t << std::endl;
        }

        // ==================== 清理测试数据 ====================
        std::cout << "\n=== 清理 ===" << std::endl;
        redis.del({"name", "temp_key", "user:1001", "tasks", "tags"});
        std::cout << "已清理所有测试 key" << std::endl;

        std::cout << "\n所有操作完成！" << std::endl;

    } catch (const Error& e) {
        std::cerr << "Redis 错误: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
