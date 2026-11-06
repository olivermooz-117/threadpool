cmake_minimum_required(VERSION 3.16)
project(threadpool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Sanitizers: cmake -DSANITIZE=thread   (or address, undefined)
set(SANITIZE "" CACHE STRING "Sanitizer to enable: thread | address | undefined")
if(SANITIZE)
  add_compile_options(-fsanitize=${SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${SANITIZE})
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

add_library(threadpool INTERFACE)
target_include_directories(threadpool INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(threadpool INTERFACE Threads::Threads)

enable_testing()
add_executable(test_thread_pool tests/test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE threadpool)
add_test(NAME thread_pool COMMAND test_thread_pool)

add_executable(bench_pool bench/bench_pool.cpp)
target_link_libraries(bench_pool PRIVATE threadpool)
