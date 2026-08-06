# =============================================================================
# CMakeLists.txt - 网关服务器构建配置
# =============================================================================
# 本文件定义网关服务器的构建规则，生成 gateway_server 主程序。
#
# 依赖项：
#   - Protobuf:      消息序列化（base.proto, user.proto, friend.proto, message.proto 等）
#   - brpc:          百度 RPC 框架（客户端调用子服务）
#   - etcd-cpp-api:  etcd 服务发现
#   - spdlog/fmt:    日志输出
#   - jsoncpp:       JSON 解析
#   - redis++:       Redis 客户端（会话鉴权）
#   - websocketpp:   WebSocket 服务器（事件通知推送）
#   - boost:         Boost.Asio（websocketpp 依赖）
#   - gflags:        命令行参数解析
#   - httplib:       HTTP 服务器（含在 common/ 目录中）
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(gateway_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# redis++ 手动指定路径
set(REDIS_PLUS_PLUS_INCLUDE_DIR /usr/local/include)
set(REDIS_PLUS_PLUS_LIB /usr/local/lib/libredis++.a)

# ==================== Protobuf 生成 ====================
# 网关需要调用所有子服务的RPC接口，因此需要包含所有proto文件
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
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
# 生成文件路径（.pb.h/.pb.cc）
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径（log.hpp、utils.hpp、etcd_client.hpp、brpc_client.hpp、redis_client.hpp、httplib.h）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# redis++ 头文件路径
include_directories(${REDIS_PLUS_PLUS_INCLUDE_DIR})

# websocketpp 头文件路径（系统默认路径）
include_directories(/usr/include)

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

# ==================== 网关服务器主程序 ====================
add_executable(gateway_server
    gateway_server.cc
    gateway_server_builder.cc
    gateway_server_impl.cc
    ${PROTO_SRC}
)

# 添加当前源码目录到头文件搜索路径（用于包含 hpp 文件）
target_include_directories(gateway_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(gateway_server
    ${BRPC_DEPS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
    ${REDIS_PLUS_PLUS_LIB}
    /usr/lib/x86_64-linux-gnu/libhiredis.a
    /usr/lib/x86_64-linux-gnu/libhiredis_ssl.a
    ${Boost_LIBRARIES}
    pthread
)
