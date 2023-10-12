cmake_minimum_required(VERSION 3.0)
project(libcoro 
    VERSION 0.9.0
    LANGUAGES CXX 
    DESCRIPTION "C++20 coroutine library"
)

include(GNUInstallDirs)

if (NOT "$ENV{version}" STREQUAL "")
    set(PROJECT_VERSION "$ENV{version}" CACHE INTERNAL "Copied from environment variable")
endif()

# Set the githooks directory to auto format and update the readme.
message("${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR} -> git config --local core.hooksPath .githooks")
execute_process(
    COMMAND git config --local core.hooksPath .githooks
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

option(LIBCORO_EXTERNAL_DEPENDENCIES "Use Cmake find_package to resolve dependencies instead of embedded libraries, Default=OFF." OFF)
option(LIBCORO_BUILD_TESTS           "Build the tests, Default=ON." ON)
option(LIBCORO_CODE_COVERAGE         "Enable code coverage, tests must also be enabled, Default=OFF" OFF)
option(LIBCORO_BUILD_EXAMPLES        "Build the examples, Default=ON." ON)
option(LIBCORO_FEATURE_THREADING     "Include multithreading features, Default=ON." ON)
option(LIBCORO_FEATURE_NETWORKING    "Include networking features, Default=ON." ON)
option(LIBCORO_FEATURE_SSL           "Include SSL encryption features, Default=ON." ON)

message("${PROJECT_NAME} LIBCORO_EXTERNAL_DEPENDENCIES = ${LIBCORO_EXTERNAL_DEPENDENCIES}")
message("${PROJECT_NAME} LIBCORO_BUILD_TESTS           = ${LIBCORO_BUILD_TESTS}")
message("${PROJECT_NAME} LIBCORO_CODE_COVERAGE         = ${LIBCORO_CODE_COVERAGE}")
message("${PROJECT_NAME} LIBCORO_BUILD_EXAMPLES        = ${LIBCORO_BUILD_EXAMPLES}")
message("${PROJECT_NAME} LIBCORO_FEATURE_THREADING     = ${LIBCORO_FEATURE_THREADING}")
message("${PROJECT_NAME} LIBCORO_FEATURE_NETWORKING    = ${LIBCORO_FEATURE_NETWORKING}")
message("${PROJECT_NAME} LIBCORO_FEATURE_SSL           = ${LIBCORO_FEATURE_SSL}")

if(LIBCORO_EXTERNAL_DEPENDENCIES)
    if(LIBCORO_FEATURE_NETWORKING)
        find_package(c-ares CONFIG REQUIRED)
    endif()
    find_package(tl-expected CONFIG REQUIRED)
else()
    if(NOT LIBCORO_BUILD_TESTS)
        # Disable testing in expected
        set(BUILD_TESTING OFF CACHE INTERNAL "")
    endif()
    if(LIBCORO_FEATURE_NETWORKING)
        set(CARES_STATIC    ON  CACHE INTERNAL "")
        set(CARES_SHARED    OFF CACHE INTERNAL "")
        set(CARES_INSTALL   OFF CACHE INTERNAL "")
        add_subdirectory(vendor/c-ares/c-ares)
    endif()
    add_subdirectory(vendor/tartanllama/expected)
endif()

set(LIBCORO_SOURCE_FILES
    include/coro/concepts/awaitable.hpp
    include/coro/concepts/buffer.hpp
    include/coro/concepts/executor.hpp
    include/coro/concepts/promise.hpp
    include/coro/concepts/range_of.hpp

    include/coro/detail/void_value.hpp

    include/coro/attribute.hpp
    include/coro/coro.hpp
    include/coro/event.hpp src/event.cpp
    include/coro/generator.hpp
    include/coro/latch.hpp
    include/coro/mutex.hpp src/mutex.cpp
    include/coro/ring_buffer.hpp
    include/coro/semaphore.hpp src/semaphore.cpp
    include/coro/shared_mutex.hpp
    include/coro/sync_wait.hpp src/sync_wait.cpp
    include/coro/task.hpp
    include/coro/task_container.hpp
    include/coro/thread_pool.hpp src/thread_pool.cpp
    include/coro/when_all.hpp
)

if(LIBCORO_FEATURE_THREADING)
    list(APPEND LIBCORO_SOURCE_FILES
        include/coro/detail/poll_info.hpp

        include/coro/fd.hpp
        include/coro/io_scheduler.hpp src/io_scheduler.cpp
        include/coro/poll.hpp
    )
endif()

if(LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_SOURCE_FILES
        include/coro/net/connect.hpp src/net/connect.cpp
        include/coro/net/dns_resolver.hpp src/net/dns_resolver.cpp
        include/coro/net/hostname.hpp
        include/coro/net/ip_address.hpp src/net/ip_address.cpp
        include/coro/net/recv_status.hpp src/net/recv_status.cpp
        include/coro/net/send_status.hpp src/net/send_status.cpp
        include/coro/net/socket.hpp src/net/socket.cpp
        include/coro/net/tcp_client.hpp src/net/tcp_client.cpp
        include/coro/net/tcp_server.hpp src/net/tcp_server.cpp
        include/coro/net/udp_peer.hpp src/net/udp_peer.cpp
    )
    if(LIBCORO_FEATURE_SSL)
        find_package(OpenSSL REQUIRED)
        list(APPEND LIBCORO_SOURCE_FILES
            include/coro/net/ssl_context.hpp src/net/ssl_context.cpp
            include/coro/net/ssl_handshake_status.hpp
        )
    endif()
endif()

add_library(${PROJECT_NAME} STATIC ${LIBCORO_SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX PREFIX "")
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PUBLIC include)
if(LIBCORO_FEATURE_THREADING)
    target_link_libraries(${PROJECT_NAME} PUBLIC pthread tl::expected)
    target_compile_definitions(${PROJECT_NAME} PUBLIC LIBCORO_FEATURE_THREADING)
endif()
if(LIBCORO_FEATURE_NETWORKING)
    target_link_libraries(${PROJECT_NAME} PUBLIC c-ares::cares)
    target_compile_definitions(${PROJECT_NAME} PUBLIC LIBCORO_FEATURE_NETWORKING)
    if(LIBCORO_FEATURE_SSL)
        target_link_libraries(${PROJECT_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(${PROJECT_NAME} PUBLIC LIBCORO_FEATURE_SSL)
    endif()
endif()

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.2.0")
        message(FATAL_ERROR "gcc version ${CMAKE_CXX_COMPILER_VERSION} is unsupported, please upgrade to at least 10.2.0")
    endif()

    target_compile_options(${PROJECT_NAME} PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
        $<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>
        $<$<COMPILE_LANGUAGE:CXX>:-fconcepts>
        $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-pipe>)
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


# generate pc file for pkg-config
set(target1 ${PROJECT_NAME})
configure_file(libcoro.pc.in libcoro.pc @ONLY)

install(TARGETS libcoro)
install(DIRECTORY include/coro TYPE INCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libcoro.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
