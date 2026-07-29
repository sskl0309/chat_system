# =============================================================================
# CMakeLists.txt - 消息服务联调测试构建配置
# =============================================================================
# 本文件定义消息存储服务联调测试的构建规则，生成联调测试可执行文件。
#
# 测试目标：
#   mq_integration_test - 消息存储服务 MQ 联调测试
#
# 测试流程：
#   1. 使用 MQClient 向 RabbitMQ 发布消息（模拟 transmit 服务行为）
#   2. 消息存储服务消费 MQ 消息并持久化到 MySQL 和 ES
#   3. 通过 RPC 接口验证数据已正确存储
#
# 覆盖场景：
#   - 文本消息存储（MQ → message_server → MySQL + ES）
#   - 图片消息存储（MQ → message_server → MySQL file_id）
#   - 文件消息存储（MQ → message_server → MySQL 元信息）
#   - 语音消息存储（MQ → message_server → MySQL file_id）
#   - 消息查询接口验证（GetRecentMsg, GetHistoryMsg, MsgSearch）
#   - 批量消息发送与验证
#
# 运行前提：
#   - RabbitMQ 服务运行在 127.0.0.1:5672
#   - MySQL 服务运行在 127.0.0.1:3306
#   - Elasticsearch 服务运行在 127.0.0.1:9200
#   - message_server 已启动在 127.0.0.1:10004
#
# 运行方式：
#   ./mq_integration_test
#   ./mq_integration_test --message_server_addr=127.0.0.1:10004 --mq_host=127.0.0.1
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(message_mq_integration_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

# ==================== 路径定义 ====================
# Integration test/ -> message/ -> server/ -> code/
set(CODE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(SERVER_DIR ${CODE_DIR}/server)

# ==================== Protobuf 生成 ====================
set(PROTO_FILES
    ${SERVER_DIR}/proto/base.proto
    ${SERVER_DIR}/proto/message.proto
    ${SERVER_DIR}/proto/user.proto
    ${SERVER_DIR}/proto/file.proto
    ${SERVER_DIR}/proto/transmit.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== 头文件包含路径 ====================
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径（mq_client.hpp, brpc_client.hpp 等）
include_directories(${SERVER_DIR}/common)

# 消息服务头文件路径
include_directories(${SERVER_DIR}/message)

# AMQP-CPP 头文件路径
include_directories(/usr/include)

# ==================== 依赖库定义 ====================

# brpc 相关库
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
set(GFLAGS_LIB /usr/lib/x86_64-linux-gnu/libgflags.a)

# ES 依赖库
set(ELASTICLIENT_LIB ${SERVER_DIR}/third_party/elasticlient/lib/libelasticlient.a)
set(CPR_LIB ${SERVER_DIR}/third_party/cpr/lib/libcpr.a)

# RabbitMQ 依赖库
set(AMQP_CPP_LIB /tmp/AMQP-CPP/build/bin/libamqpcpp.a)
set(LIBEVENT_LIB /usr/lib/x86_64-linux-gnu/libevent.so)

# =============================================================================
# 测试目标：mq_integration_test
# =============================================================================
# 消息存储服务 MQ 联调测试
add_executable(mq_integration_test
    mq_integration_test.cc
    ${PROTO_SRC}
)

target_link_libraries(mq_integration_test
    ${BRPC_LIB}
    ${GFLAGS_LIB}
    ${PROTOBUF_LIBRARIES}
    ${AMQP_CPP_LIB}
    ${LIBEVENT_LIB}
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    brpc
    leveldb
    gflags
    jsoncpp_lib
    spdlog::spdlog
    fmt::fmt
    curl
    ssl
    crypto
    pthread
)

# =============================================================================
# 输出目录设置
# =============================================================================
set_target_properties(mq_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# =============================================================================
# 测试运行说明
# =============================================================================
# 编译完成后，运行联调测试：
#
#   # 确保所有依赖服务已启动
#   # 1. 启动 message_server
#   cd ${SERVER_DIR}/message/build && ./message_server --port=10004 ...
#
#   # 2. 运行联调测试
#   cd ${CMAKE_CURRENT_BINARY_DIR} && ./mq_integration_test
#
#   # 3. 自定义参数运行
#   ./mq_integration_test \
#       --message_server_addr=127.0.0.1:10004 \
#       --mq_host=127.0.0.1 \
#       --mq_port=5672 \
#       --timeout_ms=5000
# =============================================================================