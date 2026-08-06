# =============================================================================
# CMakeLists.txt - 网关服务测试构建配置
# =============================================================================
# 本目录包含网关服务的端到端测试代码，基于 Google Test 框架通过 HTTP 请求
# 对 GatewayServer 的所有路由进行集成测试。
#
# 测试目标：
#   - gateway_server_test : 网关服务端到端 gtest 测试客户端
#     测试流程：用户注册登录 → 鉴权获取信息 → 好友操作 → 会话管理 → 消息收发 → 文件上传下载
#
# 依赖项：
#   - Protobuf: 生成所有子服务的 pb 文件
#   - gtest:     Google Test 测试框架
#   - gflags:    命令行参数解析
#   - spdlog/fmt: 日志输出
#   - httplib:   HTTP 客户端（用于向网关发送请求）
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(gateway_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# ==================== Protobuf 生成 ====================
# 网关测试需要所有子服务的 proto 定义
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../proto)
set(PROTO_FILES
    ${PROTO_DIR}/base.proto
    ${PROTO_DIR}/user.proto
    ${PROTO_DIR}/friend.proto
    ${PROTO_DIR}/message.proto
    ${PROTO_DIR}/transmit.proto
    ${PROTO_DIR}/file.proto
    ${PROTO_DIR}/speech.proto
    ${PROTO_DIR}/notify.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== 头文件包含路径 ====================
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# 项目公共头文件路径（log.hpp、httplib.h）
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
include_directories(${COMMON_DIR})

# ==================== 依赖库 ====================
set(GTEST_LIB /usr/lib/x86_64-linux-gnu/libgtest.a)
set(GFLAGS_LIB /usr/lib/x86_64-linux-gnu/libgflags.a)

set(BRPC_DEPS
    /usr/lib/x86_64-linux-gnu/libbrpc.a
    ${PROTOBUF_LIBRARIES}
    ${GFLAGS_LIB}
    /usr/lib/x86_64-linux-gnu/libleveldb.a
    /usr/lib/x86_64-linux-gnu/libssl.a
    /usr/lib/x86_64-linux-gnu/libcrypto.a
    /usr/lib/x86_64-linux-gnu/libsnappy.so.1
    pthread
    dl
    z
)

# ==================== 网关服务端到端测试 ====================
add_executable(gateway_server_test
    gateway_server_test.cc
    ${PROTO_SRC}
)

target_link_libraries(gateway_server_test
    ${GTEST_LIB}
    ${BRPC_DEPS}
    spdlog::spdlog
    fmt::fmt
)

set_target_properties(gateway_server_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
