cmake_minimum_required(VERSION 3.12)
project(libcoro_examples)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    set(LIBCORO_EXAMPLE_OPTIONS -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    set(LIBCORO_EXAMPLE_OPTIONS -Wall -Wextra -pipe)
elseif(MSVC)
    set(LIBCORO_EXAMPLE_OPTIONS /W4)
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()

add_executable(coro_task coro_task.cpp)
target_link_libraries(coro_task PUBLIC libcoro)
target_compile_options(coro_task PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_generator coro_generator.cpp)
target_link_libraries(coro_generator PUBLIC libcoro)
target_compile_options(coro_generator PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_event coro_event.cpp)
target_link_libraries(coro_event PUBLIC libcoro)
target_compile_options(coro_event PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

if(LIBCORO_FEATURE_NETWORKING)
    add_executable(coro_latch coro_latch.cpp)
    target_link_libraries(coro_latch PUBLIC libcoro)
    target_compile_options(coro_latch PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})
endif()

add_executable(coro_mutex coro_mutex.cpp)
target_link_libraries(coro_mutex PUBLIC libcoro)
target_compile_options(coro_mutex PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_thread_pool coro_thread_pool.cpp)
target_link_libraries(coro_thread_pool PUBLIC libcoro)
target_compile_options(coro_thread_pool PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_semaphore coro_semaphore.cpp)
target_link_libraries(coro_semaphore PUBLIC libcoro)
target_compile_options(coro_task PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_ring_buffer coro_ring_buffer.cpp)
target_link_libraries(coro_ring_buffer PUBLIC libcoro)
target_compile_options(coro_ring_buffer PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_shared_mutex coro_shared_mutex.cpp)
target_link_libraries(coro_shared_mutex PUBLIC libcoro)
target_compile_options(coro_shared_mutex PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_sync_wait coro_sync_wait.cpp)
target_link_libraries(coro_sync_wait PUBLIC libcoro)
target_compile_options(coro_sync_wait PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

add_executable(coro_when_all coro_when_all.cpp)
target_link_libraries(coro_when_all PUBLIC libcoro)
target_compile_options(coro_when_all PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

if(LIBCORO_FEATURE_NETWORKING)
    add_executable(coro_io_scheduler coro_io_scheduler.cpp)
    target_link_libraries(coro_io_scheduler PUBLIC libcoro)
    target_compile_options(coro_io_scheduler PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

    add_executable(coro_tcp_echo_server coro_tcp_echo_server.cpp)
    target_link_libraries(coro_tcp_echo_server PUBLIC libcoro)
    target_compile_options(coro_tcp_echo_server PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

    add_executable(coro_http_200_ok_server coro_http_200_ok_server.cpp)
    target_link_libraries(coro_http_200_ok_server PUBLIC libcoro)
    target_compile_options(coro_http_200_ok_server PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})

    if(NOT EMSCRIPTEN)
        add_executable(coro_when_any coro_when_any.cpp)
        target_link_libraries(coro_when_any PUBLIC libcoro)
        target_compile_options(coro_when_any PUBLIC ${LIBCORO_EXAMPLE_OPTIONS})
    endif()
endif()
