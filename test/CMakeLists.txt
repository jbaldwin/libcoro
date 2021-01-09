cmake_minimum_required(VERSION 3.16)
project(libcoro_test)

set(LIBCORO_TEST_SOURCE_FILES
    net/test_dns_client.cpp
    net/test_ip_address.cpp
    net/test_tcp_server.cpp
    net/test_udp_peers.cpp

    bench.cpp
    test_event.cpp
    test_generator.cpp
    test_io_scheduler.cpp
    test_latch.cpp
    test_sync_wait.cpp
    test_task.cpp
    test_thread_pool.cpp
    test_when_all.cpp
)

add_executable(${PROJECT_NAME} main.cpp ${LIBCORO_TEST_SOURCE_FILES})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE coro)
target_compile_options(${PROJECT_NAME} PUBLIC -fcoroutines)

if(CORO_CODE_COVERAGE)
    target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
    target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
endif()

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
endif()

add_test(NAME coro_test COMMAND ${PROJECT_NAME})