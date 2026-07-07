cmake_minimum_required(VERSION 3.10)
project(echo_service)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Proto file
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/main.proto)

# Generate protobuf source files
protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILE})

# Include generated proto headers
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# brpc paths
set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

# Common libraries for brpc
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

# Echo server
add_executable(echo_server echo_server.cc ${PROTO_SRC})
target_link_libraries(echo_server ${BRPC_DEPS})

# Echo client
add_executable(echo_client echo_client.cc ${PROTO_SRC})
target_link_libraries(echo_client ${BRPC_DEPS})
