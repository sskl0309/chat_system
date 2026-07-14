cmake_minimum_required(VERSION 3.10)
project(websocketpp_example)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(websocketpp REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

include_directories(${WEBSOCKETPP_INCLUDE_DIR})
include_directories(${Boost_INCLUDE_DIRS})

add_executable(ws_server ws_server.cpp)
target_link_libraries(ws_server ${Boost_LIBRARIES} pthread)

add_executable(ws_client ws_client.cpp)
target_link_libraries(ws_client ${Boost_LIBRARIES} pthread)
