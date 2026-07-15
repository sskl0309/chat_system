cmake_minimum_required(VERSION 3.10)
project(redis_example)

set(CMAKE_CXX_STANDARD 17)

# hiredis 没有自带 cmake config，使用自定义 config 文件
set(hiredis_DIR "${CMAKE_SOURCE_DIR}/cmake")

# 查找 redis++ 库
find_package(redis++ REQUIRED)

add_executable(redis_example main.cpp)
target_link_libraries(redis_example redis++::redis++ hiredis::hiredis)
