cmake_minimum_required(VERSION 3.0)
project(libcoro_test)

set(LIBCORO_TEST_SOURCE_FILES
    test_event.cpp
    test_generator.cpp
    test_latch.cpp
    test_mutex.cpp
    test_ring_buffer.cpp
    test_semaphore.cpp
    test_shared_mutex.cpp
    test_sync_wait.cpp
    test_task.cpp
    test_thread_pool.cpp
    test_when_all.cpp

    catch_amalgamated.cpp
)

if(LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        net/test_dns_resolver.cpp
        net/test_ip_address.cpp
        net/test_tcp_server.cpp
        net/test_udp_peers.cpp
    )
endif()

if(LIBCORO_FEATURE_THREADING)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        bench.cpp
        test_io_scheduler.cpp
    )
endif()

add_executable(${PROJECT_NAME} main.cpp ${LIBCORO_TEST_SOURCE_FILES})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC libcoro)
target_compile_options(${PROJECT_NAME} PUBLIC -fcoroutines)

if(LIBCORO_CODE_COVERAGE)
    target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
    target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
endif()

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
endif()

add_test(NAME libcoro_tests COMMAND ${PROJECT_NAME})
