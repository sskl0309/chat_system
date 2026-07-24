# =============================================================================
# CMakeLists.txt - 用户子服务构建配置
# =============================================================================
# 本文件定义用户子服务的构建规则，生成 user_server 主程序。
#
# 依赖项：
#   - Protobuf:      消息序列化（base.proto, user.proto, file.proto）
#   - brpc:          百度 RPC 框架
#   - etcd-cpp-api:  etcd 服务注册与发现
#   - spdlog/fmt:    日志输出
#   - jsoncpp:       JSON 解析
#   - odb/mysql:     ODB ORM 框架 + MySQL 后端
#   - redis++:       Redis 客户端
#   - OpenSSL:       SSL/TLS 加密（邮件发送）
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(user_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

# redis++ 手动指定路径（hiredis 无 cmake config，无法使用 find_package(redis++)）
set(REDIS_PLUS_PLUS_INCLUDE_DIR /usr/local/include)
set(REDIS_PLUS_PLUS_LIB /usr/local/lib/libredis++.a)

# cpr 和 elasticlient 手动指定路径（系统权限限制无法安装到系统目录）
set(CPR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpr/include)
set(CPR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpr/lib/libcpr.a)
set(ELASTICLIENT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/elasticlient/include)
set(ELASTICLIENT_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/elasticlient/lib/libelasticlient.a)

# ==================== Protobuf 生成 ====================
# user.proto 依赖 base.proto，user_service_impl.cc 需要 file.proto（调用文件服务）
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/base.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/user.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/file.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== ODB 源文件 ====================
set(ODB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/user-odb.cxx
)

# ==================== 头文件包含路径 ====================
# 生成文件路径（.pb.h/.pb.cc）
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径（log.hpp、utils.hpp、etcd_client.hpp、brpc_client.hpp 等）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ODB 头文件路径（user.hxx, user-odb.hxx）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb)

# redis++ 头文件路径
include_directories(${REDIS_PLUS_PLUS_INCLUDE_DIR})

# cpr 和 elasticlient 头文件路径
include_directories(${CPR_INCLUDE_DIR})
include_directories(${ELASTICLIENT_INCLUDE_DIR})

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

# ==================== 用户服务主程序 ====================
add_executable(user_server
    user_server.cc
    user_server_builder.cc
    user_service_impl.cc
    ${ODB_FILES}
    ${PROTO_SRC}
)

target_link_libraries(user_server
    ${BRPC_DEPS}
    ${ODB_LIBS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
    ${REDIS_PLUS_PLUS_LIB}
    /usr/lib/x86_64-linux-gnu/libhiredis.a
    /usr/lib/x86_64-linux-gnu/libhiredis_ssl.a
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    curl
    ssl
    crypto
    mysqlclient
)

# ==================== 用户服务测试客户端 ====================
# add_executable(user_client_test
#     test/user_client_test.cc
#     ${PROTO_SRC}
# )
# target_link_libraries(user_client_test
#     ${BRPC_DEPS}
#     etcd-cpp-api
#     spdlog::spdlog
#     fmt::fmt
#     jsoncpp_lib
# )

# =============================================================================
# 单元测试目标（基于 Google Test 框架）
# =============================================================================
# 测试依赖说明：
#   - gtest:  /usr/lib/x86_64-linux-gnu/libgtest.a（libgtest-dev 包提供）
#   - gflags: /usr/lib/x86_64-linux-gnu/libgflags.a
#   - pthread: gtest 多线程支持
#
# 各测试目标的库依赖：
#   - email_client_test : OpenSSL（SMTP over TLS）
#   - user_es_test       : elasticlient + cpr + jsoncpp + curl
#   - user_table_test    : ODB（user-odb.cxx）+ mysqlclient
#   - redis_client_test  : redis++ + hiredis
# =============================================================================

# gtest / gflags 静态库路径
set(GTEST_LIB /usr/lib/x86_64-linux-gnu/libgtest.a)
set(GFLAGS_LIB /usr/lib/x86_64-linux-gnu/libgflags.a)

# -------------------- email_client_test --------------------
# 测试 common/email_client.hpp（SMTP 验证码邮件发送）
add_executable(email_client_test
    test/email/email_client_test.cc
)
target_link_libraries(email_client_test
    ${GTEST_LIB}
    ${GFLAGS_LIB}
    ssl
    crypto
    spdlog::spdlog
    fmt::fmt
    pthread
)

# -------------------- user_es_test --------------------
# 测试 user/user_es.hpp（用户 ES 索引与搜索）
add_executable(user_es_test
    test/es/user_es_test.cc
)
# user_es.hpp 位于 user/ 目录下，需额外添加当前源码目录到 include 路径
target_include_directories(user_es_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(user_es_test
    ${GTEST_LIB}
    ${GFLAGS_LIB}
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    jsoncpp_lib
    curl
    ssl
    crypto
    spdlog::spdlog
    fmt::fmt
    pthread
)

# -------------------- user_table_test --------------------
# 测试 common/user_table.hpp（用户表 ODB CRUD）
# 注意：必须编译 user-odb.cxx（ODB 生成的实现代码）
add_executable(user_table_test
    test/mysql/user_table_test.cc
    ${ODB_FILES}
)
target_link_libraries(user_table_test
    ${GTEST_LIB}
    ${GFLAGS_LIB}
    ${ODB_LIBS}
    mysqlclient
    spdlog::spdlog
    fmt::fmt
    pthread
)

# -------------------- redis_client_test --------------------
# 测试 common/redis_client.hpp（会话与验证码管理）
add_executable(redis_client_test
    test/redis/redis_client_test.cc
)
target_link_libraries(redis_client_test
    ${GTEST_LIB}
    ${GFLAGS_LIB}
    ${REDIS_PLUS_PLUS_LIB}
    /usr/lib/x86_64-linux-gnu/libhiredis.a
    /usr/lib/x86_64-linux-gnu/libhiredis_ssl.a
    ssl
    crypto
    spdlog::spdlog
    fmt::fmt
    pthread
)
