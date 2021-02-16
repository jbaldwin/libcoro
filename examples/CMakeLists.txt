cmake_minimum_required(VERSION 3.0)
project(libcoro_examples)

add_executable(coro_task coro_task.cpp)
target_compile_features(coro_task PUBLIC cxx_std_20)
target_link_libraries(coro_task PUBLIC libcoro)

add_executable(coro_generator coro_generator.cpp)
target_compile_features(coro_generator PUBLIC cxx_std_20)
target_link_libraries(coro_generator PUBLIC libcoro)

add_executable(coro_event coro_event.cpp)
target_compile_features(coro_event PUBLIC cxx_std_20)
target_link_libraries(coro_event PUBLIC libcoro)

add_executable(coro_latch coro_latch.cpp)
target_compile_features(coro_latch PUBLIC cxx_std_20)
target_link_libraries(coro_latch PUBLIC libcoro)

add_executable(coro_mutex coro_mutex.cpp)
target_compile_features(coro_mutex PUBLIC cxx_std_20)
target_link_libraries(coro_mutex PUBLIC libcoro)

add_executable(coro_thread_pool coro_thread_pool.cpp)
target_compile_features(coro_thread_pool PUBLIC cxx_std_20)
target_link_libraries(coro_thread_pool PUBLIC libcoro)

add_executable(coro_io_scheduler coro_io_scheduler.cpp)
target_compile_features(coro_io_scheduler PUBLIC cxx_std_20)
target_link_libraries(coro_io_scheduler PUBLIC libcoro)

add_executable(coro_task_container coro_task_container.cpp)
target_compile_features(coro_task_container PUBLIC cxx_std_20)
target_link_libraries(coro_task_container PUBLIC libcoro)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_compile_options(coro_task           PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_generator      PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_event          PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_latch          PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_mutex          PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_thread_pool    PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_io_scheduler   PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_task_container PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()