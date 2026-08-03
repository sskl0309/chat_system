cmake_minimum_required(VERSION 3.10)
project(friend_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

set(CPR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpr/include)
set(CPR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpr/lib/libcpr.a)
set(ELASTICLIENT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/elasticlient/include)
set(ELASTICLIENT_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/elasticlient/lib/libelasticlient.a)

# Order matters: dependency files must be compiled before dependents
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/base.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/file.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/message.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/user.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/friend.proto
)

protobuf_generate_cpp(PROTO_SRC PROTO_HDR ${PROTO_FILES})

set(ODB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend/friend_relation-odb.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend/friend_event-odb.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend/chat_session-odb.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend/chat_session_member-odb.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/../odb/user/user-odb.cxx
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

set(BRPC_INCLUDE_DIR /usr/include/brpc)
set(BRPC_LIB /usr/lib/x86_64-linux-gnu/libbrpc.a)
include_directories(${BRPC_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb/friend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb/user)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../user)
include_directories(${CPR_INCLUDE_DIR})
include_directories(${ELASTICLIENT_INCLUDE_DIR})

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

set(ODB_LIBS
    /usr/lib/libodb-mysql.a
    /usr/lib/libodb-boost.a
    /usr/lib/libodb-2.5.so
)

add_executable(friend_server
    friend_server.cc
    friend_server_builder.cc
    friend_service_impl.cc
    ${ODB_FILES}
    ${PROTO_SRC}
)

target_link_libraries(friend_server
    ${BRPC_DEPS}
    ${ODB_LIBS}
    etcd-cpp-api
    spdlog::spdlog
    fmt::fmt
    jsoncpp_lib
    ${ELASTICLIENT_LIB}
    ${CPR_LIB}
    curl
    ssl
    crypto
    mysqlclient
)

set_target_properties(friend_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
