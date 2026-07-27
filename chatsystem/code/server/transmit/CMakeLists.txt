# =============================================================================
# CMakeLists.txt - 消息转发子服务构建配置
# =============================================================================
# 本文件定义消息转发子服务的构建规则，生成 transmit_server 主程序。
#
# 服务功能：
#   消息转发子服务主要用于针对一条消息内容，组织消息的ID以及各项所需要素，
#   然后告诉网关服务器一条消息应该发给谁。同时将消息放入消息队列中，
#   由消息存储管理子服务进行消费存储。
#
# 依赖项：
#   - Protobuf:      消息序列化（base.proto, transmit.proto, user.proto）
#   - brpc:          百度 RPC 框架
#   - etcd-cpp-api:  etcd 服务注册与发现
#   - spdlog/fmt:    日志输出
#   - odb/mysql:     ODB ORM 框架 + MySQL 后端
#   - amqpcpp:       RabbitMQ 客户端（消息发布）
#   - libevent:      AMQP-CPP 的事件循环库
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(transmit_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# ==================== Protobuf 生成 ====================
# transmit.proto 依赖 base.proto，transmit_service_impl.cc 需要 user.proto（调用用户服务）
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/base.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/user.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/transmit.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== ODB 源文件 ====================
# 消息转发服务使用会话表和会话成员表（与好友服务共享）
set(ODB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend/chat_session-odb.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend/chat_session_member-odb.cxx
)

# ==================== 头文件包含路径 ====================
# 生成文件路径（.pb.h/.pb.cc）
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径（log.hpp、utils.hpp、etcd_client.hpp、brpc_client.hpp、mq_client.hpp 等）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ODB 头文件路径（chat_session.hxx, chat_session_member.hxx, xxx-odb.hxx）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend)

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
# AMQP-CPP 库路径（使用本地构建的库）
set(AMQP_CPP_INCLUDE_DIR /tmp/AMQP-CPP/include)
set(AMQP_CPP_LIB /tmp/AMQP-CPP/build/bin/libamqpcpp.a)
include_directories(${AMQP_CPP_INCLUDE_DIR})

# libevent 库路径
set(LIBEVENT_INCLUDE_DIR /usr/include)
set(LIBEVENT_LIB /usr/lib/x86_64-linux-gnu/libevent.so)
include_directories(${LIBEVENT_INCLUDE_DIR})

# ==================== 消息转发服务主程序 ====================
add_executable(transmit_server
    transmit_server.cc
    transmit_server_builder.cc
    transmit_service_impl.cc
    ${ODB_FILES}
    ${PROTO_SRC}
)

target_link_libraries(transmit_server
    ${BRPC_DEPS}
    ${ODB_LIBS}
    ${AMQP_CPP_LIB}
    ${LIBEVENT_LIB}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    mysqlclient
)

# ==================== 输出目录设置 ====================
set_target_properties(transmit_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# =============================================================================
# 测试目标（端到端测试客户端）
# =============================================================================
# 测试依赖说明：
#   - brpc:      RPC 框架（客户端）
#   - protobuf:  消息序列化
#   - gflags:    命令行参数解析
#   - spdlog:    日志输出
# =============================================================================

# gflags 静态库路径
set(GFLAGS_LIB /usr/lib/x86_64-linux-gnu/libgflags.a)

# -------------------- transmit_server_test --------------------
# 消息转发服务端到端测试客户端（测试 GetTransmitTarget RPC 接口）
# 测试用例：正常请求、空用户ID、空会话ID、空消息内容
add_executable(transmit_server_test
    test/transmit_server_test.cc
    ${PROTO_SRC}
)
target_include_directories(transmit_server_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(transmit_server_test
    ${GFLAGS_LIB}
    brpc
    protobuf
    leveldb
    ssl
    crypto
    spdlog::spdlog
    fmt::fmt
    pthread
)
