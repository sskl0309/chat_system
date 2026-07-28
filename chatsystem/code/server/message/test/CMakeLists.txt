# =============================================================================
# CMakeLists.txt - 消息存储子服务测试构建配置
# =============================================================================
# 本文件定义消息存储子服务的测试构建规则，生成三个测试可执行文件：
#
# 测试目标：
#   1. message_table_test   - MySQL 数据库 CRUD 单元测试（gtest）
#   2. message_es_test       - Elasticsearch 操作单元测试（gtest）
#   3. message_service_test - RPC 接口端到端测试（独立测试客户端）
#
# 依赖项：
#   - GTest:          Google Test 测试框架
#   - Protobuf:       消息序列化（message.proto, file.proto）
#   - brpc:           RPC 框架（端到端测试）
#   - odb/mysql:      ODB ORM 框架 + MySQL 后端（数据库测试）
#   - elasticlient:  ES 客户端（ES 测试）
#   - spdlog/fmt:     日志输出
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(message_storage_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

# ==================== 路径定义（从 test/ 向上两级到 server/） ====================
set(SERVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ==================== Protobuf 生成 ====================
set(PROTO_FILES
    ${SERVER_DIR}/proto/base.proto
    ${SERVER_DIR}/proto/message.proto
    ${SERVER_DIR}/proto/user.proto
    ${SERVER_DIR}/proto/file.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== ODB 源文件 ====================
set(ODB_FILES
    ${SERVER_DIR}/odb/message/message-odb.cxx
)

# ==================== 头文件包含路径 ====================
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径
include_directories(${SERVER_DIR}/common)

# ODB 头文件路径
include_directories(${SERVER_DIR}/odb/message)

# 消息服务头文件路径（message_table.hpp, message_es.hpp 等）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# ES 客户端头文件路径
set(ELASTICLIENT_INCLUDE_DIR ${SERVER_DIR}/third_party/elasticlient/include)
include_directories(${ELASTICLIENT_INCLUDE_DIR})

# cpr 头文件路径
set(CPR_INCLUDE_DIR ${SERVER_DIR}/third_party/cpr/include)
include_directories(${CPR_INCLUDE_DIR})

# ==================== 依赖库定义 ====================

# brpc 相关库
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
set(GFLAGS_LIB /usr/lib/x86_64-linux-gnu/libgflags.a)

# ODB 依赖库
set(ODB_LIBS
    /usr/lib/libodb-mysql.a
    /usr/lib/libodb-boost.a
    /usr/lib/libodb-2.5.so
)

# ES 依赖库
set(ELASTICLIENT_LIB ${SERVER_DIR}/third_party/elasticlient/lib/libelasticlient.a)
set(CPR_LIB ${SERVER_DIR}/third_party/cpr/lib/libcpr.a)

# =============================================================================
# 测试目标 1：message_table_test（MySQL CRUD 单元测试）
# =============================================================================
# 测试 MessageTable 类的全部数据库操作接口
add_executable(message_table_test
    message_table_test.cc
    ${ODB_FILES}
    ${PROTO_SRC}
)

target_link_libraries(message_table_test
    GTest::GTest
    GTest::Main
    ${ODB_LIBS}
    ${PROTOBUF_LIBRARIES}
    spdlog::spdlog
    fmt::fmt
    gflags
    pthread
    mysqlclient
)

# =============================================================================
# 测试目标 2：message_es_test（Elasticsearch 单元测试）
# =============================================================================
# 测试 MessageES 类的全部 ES 操作接口
add_executable(message_es_test
    message_es_test.cc
    ${PROTO_SRC}
)

target_link_libraries(message_es_test
    GTest::GTest
    GTest::Main
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    ${PROTOBUF_LIBRARIES}
    jsoncpp_lib
    spdlog::spdlog
    fmt::fmt
    gflags
    curl
    ssl
    crypto
    pthread
)

# =============================================================================
# 测试目标 3：message_service_test（RPC 端到端测试）
# =============================================================================
# 测试消息存储服务的 3 个 RPC 接口：GetRecentMsg, GetHistoryMsg, MsgSearch
add_executable(message_service_test
    message_service_test.cc
    ${PROTO_SRC}
)

target_link_libraries(message_service_test
    ${BRPC_LIB}
    ${GFLAGS_LIB}
    ${PROTOBUF_LIBRARIES}
    brpc
    leveldb
    gflags
    ssl
    crypto
    spdlog::spdlog
    fmt::fmt
    pthread
)

# =============================================================================
# 输出目录设置
# =============================================================================
set_target_properties(message_table_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(message_es_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(message_service_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# =============================================================================
# 测试运行说明
# =============================================================================
# 编译完成后，可单独运行每个测试：
#
#   # 1. MySQL 数据库单元测试
#   ./message_table_test
#   ./message_table_test --mysql_user=root --mysql_password=xxx
#
#   # 2. Elasticsearch 单元测试
#   ./message_es_test
#   ./message_es_test --es_host=127.0.0.1 --es_port=9200
#
#   # 3. RPC 端到端测试（需要先启动 message_server）
#   ./message_service_test --message_server_addr=127.0.0.1:10004
#
#   # 运行全部 gtest 测试
#   cd build && ctest --output-on-failure
# =============================================================================