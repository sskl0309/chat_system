# =============================================================================
# CMakeLists.txt - 消息存储子服务构建配置
# =============================================================================
# 本文件定义消息存储子服务的构建规则，生成 message_server 主程序。
#
# 服务功能：
#   消息存储子服务主要负责：
#   1. 从 MQ 消费消息，将消息元信息存入 MySQL
#   2. 将文本消息存入 Elasticsearch（支持关键字搜索）
#   3. 将文件/语音/图片消息转储到文件子服务
#   4. 对外提供消息获取接口（最近N条、时间段、关键字搜索）
#
# 依赖项：
#   - Protobuf:      消息序列化（base.proto, message.proto, user.proto, file.proto）
#   - brpc:          百度 RPC 框架
#   - etcd-cpp-api:  etcd 服务注册与发现
#   - spdlog/fmt:    日志输出
#   - jsoncpp:       JSON 解析
#   - odb/mysql:     ODB ORM 框架 + MySQL 后端
#   - amqpcpp:       RabbitMQ 客户端（消息消费）
#   - libevent:      AMQP-CPP 的事件循环库
#   - elasticlient:  ES 客户端
#   - cpr:           HTTP 请求库（elasticlient 依赖）
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(message_storage_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

# cpr 和 elasticlient 手动指定路径
set(CPR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpr/include)
set(CPR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpr/lib/libcpr.a)
set(ELASTICLIENT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/elasticlient/include)
set(ELASTICLIENT_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/elasticlient/lib/libelasticlient.a)

# ==================== Protobuf 生成 ====================
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/base.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/message.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/user.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/file.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== ODB 源文件 ====================
set(ODB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/message/message-odb.cxx
)

# ==================== 头文件包含路径 ====================
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ODB 头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb/message)

# cpr 和 elasticlient 头文件路径
include_directories(${CPR_INCLUDE_DIR})
include_directories(${ELASTICLIENT_INCLUDE_DIR})

# AMQP-CPP 头文件路径
set(AMQP_CPP_INCLUDE_DIR /tmp/AMQP-CPP/include)
include_directories(${AMQP_CPP_INCLUDE_DIR})

# libevent 头文件路径
set(LIBEVENT_INCLUDE_DIR /usr/include)
include_directories(${LIBEVENT_INCLUDE_DIR})

# ==================== brpc 静态依赖库列表 ====================
set(BRPC_DEPS
    ${BRPC_LIB}
    ${PROTOBUF_LIBRARIES}
    /usr/lib/x86_64-linux-gnu/libgflags.a
    /usr/lib/x86_64-linux-gnu/libleveldb.a
    /usr/lib/x86_64-linux-gnu/libssl.a
    /usr/lib/x86_64-linux-gnu/libcrypto.a
    /usr/lib/x86_64-linux-gnu/libsnappy.so.1
    pthread
    dl
    z
)

# ==================== ODB 依赖库 ====================
set(ODB_LIBS
    /usr/lib/libodb-mysql.a
    /usr/lib/libodb-boost.a
    /usr/lib/libodb-2.5.so
)

# ==================== RabbitMQ 依赖库 ====================
set(AMQP_CPP_LIB /tmp/AMQP-CPP/build/bin/libamqpcpp.a)
set(LIBEVENT_LIB /usr/lib/x86_64-linux-gnu/libevent.so)

# ==================== 消息存储服务主程序 ====================
add_executable(message_server
    message_server.cc
    message_server_builder.cc
    message_service_impl.cc
    ${ODB_FILES}
    ${PROTO_SRC}
)

target_link_libraries(message_server
    ${BRPC_DEPS}
    ${ODB_LIBS}
    ${AMQP_CPP_LIB}
    ${LIBEVENT_LIB}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    curl
    ssl
    crypto
    mysqlclient
)

# ==================== 输出目录设置 ====================
set_target_properties(message_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)