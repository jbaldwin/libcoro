cmake_minimum_required(VERSION 3.12)
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
        net/test_ip_address.cpp
    )

    # These tests require coro::io_scheduler
    if(LIBCORO_FEATURE_PLATFORM)
        list(APPEND LIBCORO_TEST_SOURCE_FILES
            net/test_dns_resolver.cpp
            net/test_tcp_server.cpp
            net/test_tls_server.cpp
            net/test_udp_peers.cpp
        )
    endif()
endif()

if(LIBCORO_FEATURE_PLATFORM)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        bench.cpp # networking is ifdef'ed internally
        test_io_scheduler.cpp
    )
endif()

add_executable(${PROJECT_NAME} main.cpp ${LIBCORO_TEST_SOURCE_FILES})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(LIBCORO_TEST_LINK_LIBRARIES libcoro)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    set(LIBCORO_TEST_COMPILE_OPTIONS -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    set(LIBCORO_TEST_COMPILE_OPTIONS -Wall -Wextra -pipe)
elseif(MSVC)
    set(LIBCORO_TEST_COMPILE_OPTIONS /W4)
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()

if(LIBCORO_CODE_COVERAGE)
    list(APPEND LIBCORO_TEST_LINK_LIBRARIES gcov)
    list(APPEND LIBCORO_TEST_COMPILE_OPTIONS --coverage)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBCORO_TEST_LINK_LIBRARIES})
target_compile_options(${PROJECT_NAME} PUBLIC ${LIBCORO_TEST_COMPILE_OPTIONS})

add_test(NAME libcoro_tests COMMAND ${PROJECT_NAME})
