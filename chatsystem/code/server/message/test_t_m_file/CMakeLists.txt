# =============================================================================
# CMakeLists.txt - 消息+文件服务联调测试构建配置
# =============================================================================
# 本目录进行消息存储服务与文件服务的联合调试测试。
#
# 测试目标：
#   file_integration_test - 消息与文件服务联调测试
#
# 测试场景：
#   1. 图片消息上传：MQ -> message_server -> file_server（PutSingleFile）
#   2. 文件消息上传：MQ -> message_server -> file_server（PutSingleFile）
#   3. 语音消息上传：MQ -> message_server -> file_server（PutSingleFile）
#   4. 图片消息下载：message_server -> file_server（GetSingleFile）
#   5. 文件消息元数据验证
#   6. 语音消息下载验证
#
# 运行前提：
#   - etcd 服务运行在 127.0.0.1:2379
#   - RabbitMQ 服务运行在 127.0.0.1:5672
#   - MySQL 服务运行在 127.0.0.1:3306
#   - Elasticsearch 服务运行在 127.0.0.1:9200
#   - file_server 已启动在 127.0.0.1:10005（注册为 file_service）
#   - message_server 已启动在 127.0.0.1:10004（配置 etcd 服务发现）
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(message_file_integration_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

# ==================== 路径定义 ====================
# test_t_m_file/ -> message/ -> server/ -> code/
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

# 项目公共头文件路径
include_directories(${SERVER_DIR}/common)

# 消息服务头文件路径
include_directories(${SERVER_DIR}/message)

# 文件服务头文件路径
include_directories(${SERVER_DIR}/file)

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

# ODB 依赖库
set(ODB_LIBS
    /usr/lib/libodb-mysql.a
    /usr/lib/libodb-boost.a
    /usr/lib/libodb-2.5.so
)

# =============================================================================
# 测试目标：file_integration_test
# =============================================================================
add_executable(file_integration_test
    file_integration_test.cc
    ${PROTO_SRC}
)

target_link_libraries(file_integration_test
    ${BRPC_LIB}
    ${GFLAGS_LIB}
    ${PROTOBUF_LIBRARIES}
    ${AMQP_CPP_LIB}
    ${LIBEVENT_LIB}
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    ${ODB_LIBS}
    brpc
    leveldb
    gflags
    jsoncpp_lib
    spdlog::spdlog
    fmt::fmt
    curl
    ssl
    crypto
    mysqlclient
    pthread
)

# =============================================================================
# 输出目录设置
# =============================================================================
set_target_properties(file_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# =============================================================================
# 运行说明
# =============================================================================
#   ./file_integration_test
#   ./file_integration_test --message_server_addr=127.0.0.1:10004
#   ./file_integration_test --file_server_addr=127.0.0.1:10005
#   ./file_integration_test --mq_host=127.0.0.1
# =============================================================================