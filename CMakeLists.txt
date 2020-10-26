cmake_minimum_required(VERSION 3.16)
project(coro CXX)

option(CORO_BUILD_TESTS    "Build the tests, Default=ON." ON)
option(CORO_CODE_COVERAGE  "Enable code coverage, tests must also be enabled, Default=OFF" OFF)

message("${PROJECT_NAME} CORO_BUILD_TESTS      = ${CORO_BUILD_TESTS}")
message("${PROJECT_NAME} CORO_CODE_COVERAGE    = ${CORO_CODE_COVERAGE}")

set(LIBCORO_SOURCE_FILES
    inc/coro/detail/void_value.hpp

    inc/coro/awaitable.hpp
    inc/coro/coro.hpp
    inc/coro/event.hpp src/event.cpp
    inc/coro/generator.hpp
    inc/coro/latch.hpp
    inc/coro/promise.hpp
    inc/coro/scheduler.hpp
    inc/coro/shutdown.hpp
    inc/coro/sync_wait.hpp src/sync_wait.cpp
    inc/coro/task.hpp
    inc/coro/thread_pool.hpp src/thread_pool.cpp
    inc/coro/when_all.hpp
)

add_library(${PROJECT_NAME} STATIC ${LIBCORO_SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PUBLIC inc)
target_link_libraries(${PROJECT_NAME} PUBLIC pthread)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.2.0")
        message(FATAL_ERROR "gcc version ${CMAKE_CXX_COMPILER_VERSION} is unsupported, please upgrade to at least 10.2.0")
    endif()

    target_compile_options(${PROJECT_NAME} PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
endif()


if(CORO_BUILD_TESTS)
    if(CORO_CODE_COVERAGE)
        target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
        target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
    endif()

    enable_testing()
    add_subdirectory(test)
endif()