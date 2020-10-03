cmake_minimum_required(VERSION 3.16)
project(libcoro_test)

set(LIBCORO_TEST_SOURCE_FILES
    bench.cpp
    test_event.cpp
    test_latch.cpp
    test_scheduler.cpp
    test_sync_wait.cpp
    test_task.cpp
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
    target_compile_options(
        ${PROJECT_NAME} PRIVATE
        -Wno-unknown-pragmas
    )
endif()
if(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Weffc++
        -Werror
        -Wpedantic
        -pedantic-errors
    )
endif()

add_test(NAME corohttp_test COMMAND ${PROJECT_NAME})