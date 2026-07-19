cmake_minimum_required(VERSION 3.10)
project(speech_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/speech.proto)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILE})

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../speech)

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

add_executable(speech_server
    speech_server.cc
    speech_server_builder.cc
    speech_service_impl.cc
    ${PROTO_SRC}
)

target_link_libraries(speech_server
    ${BRPC_DEPS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
    curl
    crypto
)

add_executable(speech_client_test
    test/speech_client_test.cc
    ${PROTO_SRC}
)

target_link_libraries(speech_client_test
    ${BRPC_DEPS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
)
