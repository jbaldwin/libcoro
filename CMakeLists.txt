cmake_minimum_required(VERSION 3.0)
project(libcoro CXX)

# Set the githooks directory to auto format and update the readme.
message("${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR} -> git config --local core.hooksPath .githooks")
execute_process(
    COMMAND git config --local core.hooksPath .githooks
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

option(LIBCORO_BUILD_TESTS    "Build the tests, Default=ON." ON)
option(LIBCORO_CODE_COVERAGE  "Enable code coverage, tests must also be enabled, Default=OFF" OFF)
option(LIBCORO_BUILD_EXAMPLES "Build the examples, Default=ON." ON)

message("${PROJECT_NAME} LIBCORO_BUILD_TESTS      = ${LIBCORO_BUILD_TESTS}")
message("${PROJECT_NAME} LIBCORO_CODE_COVERAGE    = ${LIBCORO_CODE_COVERAGE}")
message("${PROJECT_NAME} LIBCORO_BUILD_EXAMPLES   = ${LIBCORO_BUILD_EXAMPLES}")

set(CARES_STATIC    ON  CACHE INTERNAL "")
set(CARES_SHARED    OFF CACHE INTERNAL "")
set(CARES_INSTALL   OFF CACHE INTERNAL "")

add_subdirectory(vendor/c-ares/c-ares)

set(LIBCORO_SOURCE_FILES
    inc/coro/concepts/awaitable.hpp
    inc/coro/concepts/buffer.hpp
    inc/coro/concepts/executor.hpp
    inc/coro/concepts/promise.hpp
    inc/coro/concepts/range_of.hpp

    inc/coro/detail/poll_info.hpp
    inc/coro/detail/void_value.hpp

    inc/coro/net/connect.hpp src/net/connect.cpp
    inc/coro/net/dns_resolver.hpp src/net/dns_resolver.cpp
    inc/coro/net/hostname.hpp
    inc/coro/net/ip_address.hpp src/net/ip_address.cpp
    inc/coro/net/recv_status.hpp src/net/recv_status.cpp
    inc/coro/net/send_status.hpp src/net/send_status.cpp
    inc/coro/net/socket.hpp src/net/socket.cpp
    inc/coro/net/ssl_context.hpp src/net/ssl_context.cpp
    inc/coro/net/ssl_handshake_status.hpp
    inc/coro/net/tcp_client.hpp src/net/tcp_client.cpp
    inc/coro/net/tcp_server.hpp src/net/tcp_server.cpp
    inc/coro/net/udp_peer.hpp src/net/udp_peer.cpp

    inc/coro/coro.hpp
    inc/coro/event.hpp src/event.cpp
    inc/coro/fd.hpp
    inc/coro/generator.hpp
    inc/coro/io_scheduler.hpp src/io_scheduler.cpp
    inc/coro/latch.hpp
    inc/coro/mutex.hpp src/mutex.cpp
    inc/coro/poll.hpp
    inc/coro/ring_buffer.hpp
    inc/coro/semaphore.hpp src/semaphore.cpp
    inc/coro/shared_mutex.hpp
    inc/coro/stop_signal.hpp
    inc/coro/sync_wait.hpp src/sync_wait.cpp
    inc/coro/task_container.hpp
    inc/coro/task.hpp
    inc/coro/thread_pool.hpp src/thread_pool.cpp
    inc/coro/when_all.hpp
)

add_library(${PROJECT_NAME} STATIC ${LIBCORO_SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PUBLIC inc)
target_link_libraries(${PROJECT_NAME} PUBLIC pthread c-ares ssl crypto)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.2.0")
        message(FATAL_ERROR "gcc version ${CMAKE_CXX_COMPILER_VERSION} is unsupported, please upgrade to at least 10.2.0")
    endif()

    target_compile_options(${PROJECT_NAME} PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
endif()

if(LIBCORO_BUILD_TESTS)
    if(LIBCORO_CODE_COVERAGE)
        target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
        target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
    endif()

    enable_testing()
    add_subdirectory(test)
endif()

if(LIBCORO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()