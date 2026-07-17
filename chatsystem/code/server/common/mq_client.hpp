#ifndef MQ_CLIENT_HPP
#define MQ_CLIENT_HPP

#include <event2/event.h>
#include <amqpcpp.h>
#include <amqpcpp/libevent.h>
#include <functional>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>

namespace mq {

/**
 * @brief 消息回调函数类型定义
 * @param message 接收到的消息内容
 * @param deliveryTag 消息的投递标签，用于确认消息
 */
using MessageCallback = std::function<void(const std::string& message, uint64_t deliveryTag)>;

/**
 * @brief MQClient 类 - AMQP-CPP 的二次封装类
 * 
 * 本类对 AMQP-CPP 库进行了轻量级封装，将事件循环(event_base)、通信连接(connection)
 * 和信道(channel)封装在一起，提供简洁的接口用于 RabbitMQ 的消息发布和订阅。
 * 
 * 封装的核心功能：
 * 1. 声明指定交换机与队列，并进行绑定
 * 2. 向指定交换机发布消息
 * 3. 订阅指定队列消息，并设置回调函数进行消息消费处理
 */
class MQClient {
public:
    /**
     * @brief 构造函数
     * @param host RabbitMQ 服务器地址
     * @param port RabbitMQ 服务器端口，默认 5672
     * @param user 用户名
     * @param password 密码
     * @param vhost 虚拟主机，默认 "/"
     */
    MQClient(const std::string& host, uint16_t port, 
             const std::string& user, const std::string& password,
             const std::string& vhost = "/")
        : _host(host), _port(port), _user(user), _password(password), _vhost(vhost),
          _evbase(nullptr), _connected(false) {}

    /**
     * @brief 析构函数，自动停止客户端并释放资源
     */
    ~MQClient() {
        stop();
    }

    /**
     * @brief 禁止拷贝构造和赋值
     */
    MQClient(const MQClient&) = delete;
    MQClient& operator=(const MQClient&) = delete;

    /**
     * @brief 启动 MQ 客户端
     * 
     * 创建事件循环、连接处理器、TCP连接和信道，并启动事件循环线程。
     * @return 启动成功返回 true，失败返回 false
     */
    bool start() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 如果已经连接，直接返回
        if (_connected) {
            return true;
        }

        // 创建 libevent 事件循环
        _evbase = event_base_new();
        if (!_evbase) {
            return false;
        }

        // 创建 AMQP-CPP 的 libevent 处理器
        _handler = std::make_unique<AMQP::LibEventHandler>(_evbase);
        
        // 构建 AMQP 地址字符串：amqp://user:password@host:port/vhost
        std::string addr_str = "amqp://" + _user + ":" + _password + "@" + _host + ":" + 
                               std::to_string(_port) + _vhost;
        // 创建 TCP 连接
        _connection = std::make_unique<AMQP::TcpConnection>(_handler.get(), AMQP::Address(addr_str));
        
        // 创建信道
        _channel = std::make_unique<AMQP::TcpChannel>(_connection.get());
        
        // 设置信道就绪回调：连接成功时设置 _connected 标志
        _channel->onReady([this]() {
            std::lock_guard<std::mutex> lock(_mutex);
            _connected = true;
        });

        // 设置信道错误回调：发生错误时清除 _connected 标志
        _channel->onError([this](const char* message) {
            std::lock_guard<std::mutex> lock(_mutex);
            _connected = false;
        });

        // 在单独的线程中运行事件循环
        _thread = std::thread([this]() {
            event_base_dispatch(_evbase);
        });

        return true;
    }

    /**
     * @brief 停止 MQ 客户端
     * 
     * 关闭连接、退出事件循环、等待线程结束并释放资源。
     */
    void stop() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 关闭 AMQP 连接
        if (_connection) {
            _connection->close();
        }
        
        // 退出事件循环
        if (_evbase) {
            event_base_loopexit(_evbase, nullptr);
        }
        
        // 等待事件循环线程结束
        if (_thread.joinable()) {
            _thread.join();
        }
        
        // 释放事件循环资源
        if (_evbase) {
            event_base_free(_evbase);
            _evbase = nullptr;
        }
        
        // 标记连接已断开
        _connected = false;
    }

    /**
     * @brief 检查客户端是否已连接
     * @return 已连接返回 true，否则返回 false
     */
    bool is_connected() const {
        return _connected.load();
    }

    /**
     * @brief 声明交换机、队列并进行绑定
     * 
     * 按顺序执行：声明交换机 -> 声明队列 -> 将队列绑定到交换机
     * @param exchangeName 交换机名称
     * @param type 交换机类型，默认 direct（直连型）
     *             可选值：AMQP::direct, AMQP::fanout, AMQP::topic, AMQP::headers
     * @param queueName 队列名称，若为空则由服务器自动生成
     * @param routingKey 路由键，用于绑定队列和交换机
     * @return 操作成功返回 true，失败返回 false
     */
    bool declareExchangeAndBind(const std::string& exchangeName, 
                                AMQP::ExchangeType type = AMQP::direct,
                                const std::string& queueName = "",
                                const std::string& routingKey = "") {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 检查信道是否可用
        if (!_channel || !_channel->usable()) {
            return false;
        }

        // 声明交换机（持久化）
        _channel->declareExchange(exchangeName, type, AMQP::durable);

        // 声明队列
        std::string actualQueueName = queueName;
        if (actualQueueName.empty()) {
            // 队列名为空时，由服务器自动生成队列名
            _channel->declareQueue(AMQP::durable).onSuccess([&actualQueueName](const std::string& name, 
                                                                               uint32_t messageCount, 
                                                                               uint32_t consumerCount) {
                actualQueueName = name;
            });
        } else {
            // 使用指定的队列名
            _channel->declareQueue(actualQueueName, AMQP::durable);
        }

        // 将队列绑定到交换机（如果提供了路由键和队列名）
        if (!routingKey.empty() && !actualQueueName.empty()) {
            _channel->bindQueue(exchangeName, actualQueueName, routingKey);
        }

        return true;
    }

    /**
     * @brief 向指定交换机发布消息
     * @param exchangeName 交换机名称
     * @param routingKey 路由键
     * @param message 消息内容
     * @return 发布成功返回 true，失败返回 false
     */
    bool publish(const std::string& exchangeName, 
                 const std::string& routingKey, 
                 const std::string& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 检查信道是否可用
        if (!_channel || !_channel->usable()) {
            return false;
        }

        // 发布消息到交换机
        return _channel->publish(exchangeName, routingKey, message);
    }

    /**
     * @brief 订阅指定队列的消息
     * @param queueName 队列名称
     * @param callback 消息回调函数，当收到消息时会调用此函数
     * @param autoAck 是否自动确认消息，默认 false（需要手动调用 ack 确认）
     * @return 订阅成功返回 true，失败返回 false
     */
    bool consume(const std::string& queueName, 
                 const MessageCallback& callback,
                 bool autoAck = false) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 检查信道是否可用
        if (!_channel || !_channel->usable()) {
            return false;
        }

        // 设置消费标志
        int flags = 0;
        if (autoAck) {
            // 自动确认模式，无需手动调用 ack
            flags |= AMQP::noack;
        }

        // 开始消费队列，设置消息接收回调
        _channel->consume(queueName, "", flags)
                .onReceived([callback](const AMQP::Message& message, 
                                      uint64_t deliveryTag, 
                                      bool redelivered) {
                    // 将 AMQP 消息转换为 std::string 并调用用户回调
                    std::string msg(message.body(), message.bodySize());
                    callback(msg, deliveryTag);
                });

        return true;
    }

    /**
     * @brief 确认消息已处理
     * 
     * 在非自动确认模式下，处理完消息后需要调用此方法确认，
     * 否则消息会留在队列中，客户端断开后会重新投递。
     * @param deliveryTag 消息的投递标签（从回调函数中获取）
     * @return 确认成功返回 true，失败返回 false
     */
    bool ack(uint64_t deliveryTag) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 检查信道是否可用
        if (!_channel || !_channel->usable()) {
            return false;
        }

        // 发送确认
        return _channel->ack(deliveryTag);
    }

    /**
     * @brief 拒绝消息
     * 
     * 拒绝处理消息，可选择是否重新入队。
     * @param deliveryTag 消息的投递标签（从回调函数中获取）
     * @param requeue 是否将消息重新入队，默认 true（重新入队）
     * @return 拒绝成功返回 true，失败返回 false
     */
    bool reject(uint64_t deliveryTag, bool requeue = true) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // 检查信道是否可用
        if (!_channel || !_channel->usable()) {
            return false;
        }

        // 设置拒绝标志
        int flags = 0;
        if (requeue) {
            // 重新入队，消息会被其他消费者接收
            flags |= AMQP::requeue;
        }

        // 发送拒绝
        return _channel->reject(deliveryTag, flags);
    }

private:
    // ========== 连接配置参数 ==========
    std::string _host;          // RabbitMQ 服务器地址
    uint16_t _port;             // RabbitMQ 服务器端口
    std::string _user;          // 用户名
    std::string _password;      // 密码
    std::string _vhost;         // 虚拟主机

    // ========== AMQP-CPP 核心对象 ==========
    struct event_base* _evbase;                     // libevent 事件循环
    std::unique_ptr<AMQP::LibEventHandler> _handler; // AMQP-CPP 的 libevent 处理器
    std::unique_ptr<AMQP::TcpConnection> _connection; // TCP 连接对象
    std::unique_ptr<AMQP::TcpChannel> _channel;       // 信道对象
    std::thread _thread;                             // 事件循环线程
    
    // ========== 线程同步与状态 ==========
    std::mutex _mutex;           // 互斥锁，保护共享资源
    std::atomic<bool> _connected; // 连接状态标志（原子操作，线程安全）
};

} // namespace mq

#endif // MQ_CLIENT_HPP