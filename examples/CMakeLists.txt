cmake_minimum_required(VERSION 3.16)
project(libcoro_examples)

add_executable(coro_event coro_event.cpp)
target_compile_features(coro_event PUBLIC cxx_std_20)
target_link_libraries(coro_event PUBLIC libcoro)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_compile_options(coro_event PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()