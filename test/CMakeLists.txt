cmake_minimum_required(VERSION 3.12)
project(libcoro_test)

set(LIBCORO_TEST_SOURCE_FILES
        concepts/test_concepts.cpp

        test_condition_variable.cpp
        test_event.cpp
        test_generator.cpp
        test_invoke.cpp
        test_latch.cpp
        test_mutex.cpp
        test_queue.cpp
        test_ring_buffer.cpp
        test_semaphore.cpp
        test_shared_mutex.cpp
        test_sync_wait.cpp
        test_task.cpp
        test_task_group.cpp
        test_thread_pool.cpp
        test_when_all.cpp

        catch_amalgamated.hpp catch_amalgamated.cpp
        catch_extensions.hpp catch_extensions.cpp
)

if (NOT EMSCRIPTEN)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
            test_when_any.cpp
    )
endif ()

if (LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
            net/test_ip_address.cpp
            net/test_io_status.cpp
            net/test_endpoint.cpp

            net/catch_net_extensions.hpp
    )

    # These tests require coro::scheduler
    list(APPEND LIBCORO_TEST_SOURCE_FILES
            net/test_dns_resolver.cpp
            net/test_tcp_server.cpp
            net/test_tls_server.cpp
            net/test_udp_peers.cpp
    )
endif ()

if (LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
            bench.cpp
            test_scheduler.cpp
    )
endif ()

if (LIBCORO_COLLECT_TEST_SOURCES_ONLY)
    # When collecting sources for an external harness (e.g. Android JNI shared lib),
    # just expose list and skip adding executable & tests.
    # Variables are already in parent scope because of include(); nothing more needed.
else ()
    add_executable(${PROJECT_NAME} main.cpp ${LIBCORO_TEST_SOURCE_FILES})
    target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE libcoro)

    if (${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
        target_compile_options(${PROJECT_NAME} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
                $<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>
                $<$<COMPILE_LANGUAGE:CXX>:-fconcepts>
                $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
                $<$<COMPILE_LANGUAGE:CXX>:-Wall>
                $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                $<$<COMPILE_LANGUAGE:CXX>:-pipe>
        )
    elseif (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
                $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
                $<$<COMPILE_LANGUAGE:CXX>:-Wall>
                $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                $<$<COMPILE_LANGUAGE:CXX>:-pipe>
        )
    elseif (MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    else ()
        message(FATAL_ERROR "Unsupported compiler.")
    endif ()

    if (LIBCORO_CODE_COVERAGE)
        target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
        target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
    endif ()

    add_test(NAME libcoro_tests COMMAND ${PROJECT_NAME})
    set_tests_properties(libcoro_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<$<BOOL:${WIN32}>:$<TARGET_FILE_DIR:libcoro>>")
endif ()
