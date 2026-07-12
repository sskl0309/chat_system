cmake_minimum_required(VERSION 3.10)
project(etcd_test)

set(CMAKE_CXX_STANDARD 17)

find_package(etcd-cpp-api REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/..)

add_executable(test_etcd_client test_etcd_client.cpp)
target_link_libraries(test_etcd_client etcd-cpp-api spdlog::spdlog fmt::fmt)

add_executable(test_register test_register.cpp)
target_link_libraries(test_register etcd-cpp-api spdlog::spdlog fmt::fmt)

add_executable(test_discover test_discover.cpp)
target_link_libraries(test_discover etcd-cpp-api spdlog::spdlog fmt::fmt)