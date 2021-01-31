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

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_compile_options(coro_task      PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_generator PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_event     PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_latch     PUBLIC -fcoroutines -Wall -Wextra -pipe)
    target_compile_options(coro_mutex     PUBLIC -fcoroutines -Wall -Wextra -pipe)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    message(FATAL_ERROR "Clang is currently not supported.")
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()