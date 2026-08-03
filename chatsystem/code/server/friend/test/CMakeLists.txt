# =============================================================================
# CMakeLists.txt - 好友服务测试构建配置
# =============================================================================
# 本目录包含好友服务的端到端测试代码，基于 Google Test 框架测试
# FriendService 的全部 9 个 RPC 接口。
#
# 测试目标：
#   - friend_server_test : 好友服务端到端 gtest 测试客户端
#     测试流程：注册用户 → 好友申请 → 处理申请 → 搜索 → 删除 → 创建群聊 → 获取会话
#
# 依赖项：
#   - Protobuf: 生成 friend.pb.h / user.pb.h / file.pb.h 等
#   - brpc:      RPC 客户端框架
#   - gtest:     Google Test 测试框架
#   - gflags:    命令行参数解析
#   - spdlog/fmt: 日志输出
# =============================================================================

cmake_minimum_required(VERSION 3.10)
project(friend_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找依赖包 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# ==================== Protobuf 生成 ====================
# 与主服务 CMakeLists.txt 保持相同的 proto 文件顺序
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../proto)
set(PROTO_FILES
    ${PROTO_DIR}/base.proto
    ${PROTO_DIR}/file.proto
    ${PROTO_DIR}/message.proto
    ${PROTO_DIR}/user.proto
    ${PROTO_DIR}/friend.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

# ==================== 头文件包含路径 ====================
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc 头文件路径
set(BRPC_INCLUDE_DIR /usr/include/brpc)
include_directories(${BRPC_INCLUDE_DIR})

# 项目公共头文件路径
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
include_directories(${COMMON_DIR})

# user 目录头文件路径（用户服务 stub）
set(USER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../user)
include_directories(${USER_DIR})

# ==================== 依赖库 ====================
# gtest / gflags 静态库路径
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

# ==================== 好友服务端到端测试 ====================
add_executable(friend_server_test
    friend_server_test.cc
    ${PROTO_SRC}
)

target_link_libraries(friend_server_test
    ${GTEST_LIB}
    ${BRPC_DEPS}
    spdlog::spdlog
    fmt::fmt
)

set_target_properties(friend_server_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
