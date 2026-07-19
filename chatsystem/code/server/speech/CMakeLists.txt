# 语音识别服务 CMake 构建配置文件
# 配置编译选项、依赖库和链接参数

# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)
# 项目名称
project(speech_service)

# 设置 C++ 标准为 C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 查找系统依赖 ====================
# 查找 Protobuf 库（用于序列化）
find_package(Protobuf REQUIRED)
# 查找 Threads 库（用于多线程支持）
find_package(Threads REQUIRED)

# ==================== Protobuf 文件处理 ====================
# 指定 proto 文件路径
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/speech.proto)
# 自动生成 C++ 源码和头文件
protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILE})

# ==================== 头文件搜索路径 ====================
# 包含生成的 protobuf 头文件目录
include_directories(${CMAKE_CURRENT_BINARY_DIR})
# 包含系统 Protobuf 头文件目录
include_directories(${PROTOBUF_INCLUDE_DIRS})
# 包含项目公共模块目录（log.hpp, etcd_client.hpp, speech_client.hpp）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ==================== brpc 框架配置 ====================
# brpc 头文件目录
set(BRPC_INCLUDE_DIR /usr/include/brpc)
# brpc 静态库路径
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
# 包含 brpc 头文件目录
include_directories(${BRPC_INCLUDE_DIR})

# ==================== 百度AI语音SDK配置 ====================
# 百度语音 SDK 头文件目录（aip-cpp-sdk-4.16.7）
set(AIP_SDK_INCLUDE_DIR /home/sskl/chat_system/speech/aip-cpp-sdk-4.16.7)
include_directories(${AIP_SDK_INCLUDE_DIR})

# ==================== brpc 依赖库 ====================
set(BRPC_DEPS
    ${BRPC_LIB}                  # brpc 核心库
    ${PROTOBUF_LIBRARIES}        # Protobuf 库
    /usr/lib/x86_64-linux-gnu/libgflags.a    # gflags 命令行参数解析库
    /usr/lib/x86_64-linux-gnu/libleveldb.a   # LevelDB 键值存储库
    /usr/lib/x86_64-linux-gnu/libssl.a       # OpenSSL SSL/TLS 库
    /usr/lib/x86_64-linux-gnu/libcrypto.a    # OpenSSL 加密库
    /usr/lib/x86_64-linux-gnu/libsnappy.so.1 # Snappy 压缩库
    pthread                      # POSIX 线程库
    dl                           # 动态链接库
    z                            # zlib 压缩库
)

# ==================== etcd 依赖库 ====================
set(ETCD_DEPS
    /usr/lib/x86_64-linux-gnu/libetcd-cpp-api.so  # etcd C++ 客户端库
    /usr/lib/x86_64-linux-gnu/libgrpc++.so        # gRPC C++ 库
    /usr/lib/x86_64-linux-gnu/libgrpc.so          # gRPC 核心库
    /usr/lib/x86_64-linux-gnu/libprotobuf.so      # Protobuf 动态库
)

# ==================== 语音识别依赖库 ====================
set(SPEECH_DEPS
    /usr/lib/x86_64-linux-gnu/libcurl.so   # cURL HTTP 客户端库
    /usr/lib/x86_64-linux-gnu/libcrypto.so # OpenSSL 加密库
    /usr/lib/x86_64-linux-gnu/libjsoncpp.so # JSON 解析库
)

# ==================== 构建可执行文件 ====================
add_executable(speech_server 
    speech_server.cc          # 服务器主程序
    speech_service_impl.cc    # 语音识别服务实现
    ${PROTO_SRC}              # 自动生成的 protobuf 源码
)

# ==================== 链接依赖库 ====================
target_link_libraries(speech_server 
    ${BRPC_DEPS}    # brpc 框架及依赖
    ${ETCD_DEPS}    # etcd 客户端及依赖
    ${SPEECH_DEPS}  # 语音识别 SDK 依赖
)
