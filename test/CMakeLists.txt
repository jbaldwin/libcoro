cmake_minimum_required(VERSION 3.16)
project(libcoro_test)

set(LIBCORO_TEST_SOURCE_FILES
    bench.cpp
    test_event.cpp
    test_generator.cpp
    test_latch.cpp
    test_scheduler.cpp
    test_sync_wait.cpp
    test_task.cpp
    test_thread_pool.cpp
    test_when_all.cpp
)

add_executable(${PROJECT_NAME} main.cpp ${LIBCORO_TEST_SOURCE_FILES})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
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

add_test(NAME corohttp_test COMMAND ${PROJECT_NAME})