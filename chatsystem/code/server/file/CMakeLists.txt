# =============================================================================
# CMakeLists.txt - 文件子服务构建配置
# =============================================================================
# 本文件定义文件子服务的构建规则，包括：
#   - file_server:       文件服务主程序
#   - file_client_test:  文件服务测试客户端
#
# 依赖项：
#   - Protobuf:  消息序列化
#   - brpc:      百度 RPC 框架
#   - etcd-cpp-api: etcd 服务注册与发现
#   - spdlog/fmt: 日志输出
#   - jsoncpp:   JSON 解析（etcd 客户端依赖）
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(file_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(GTest REQUIRED)

# ==================== Protobuf 生成 ====================
# file.proto 依赖 base.proto，需同时生成两个 proto 的代码
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/base.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/file.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== 头文件包含路径 ====================
# 生成文件路径（.pb.h/.pb.cc）
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径（log.hpp、utils.hpp、etcd_client.hpp 等）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ==================== brpc 静态依赖库列表 ====================
# brpc 静态库依赖以下系统库
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

# ==================== 文件服务主程序 ====================
# 文件服务主程序入口，对外提供 RPC 接口
add_executable(file_server
    file_server.cc
    file_server_builder.cc
    file_service_impl.cc
    ${PROTO_SRC}
)

target_link_libraries(file_server
    ${BRPC_DEPS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
)

# ==================== 文件服务测试客户端 ====================
# 用于测试文件服务的上传与下载功能
add_executable(file_client_test
    test/file_client_test.cc
    ${PROTO_SRC}
)

target_link_libraries(file_client_test
    ${BRPC_DEPS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
    GTest::GTest
    GTest::Main
)
