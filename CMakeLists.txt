cmake_minimum_required(VERSION 3.15)
include(CMakeDependentOption)
project(libcoro
    VERSION 0.11.1
    LANGUAGES CXX
    DESCRIPTION "C++20 coroutine library"
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
    # We need to build with clang >= 17, assuming its installed by
    # brew. We also need to force linking to libc++.a
    add_link_options(-L/usr/local/opt/llvm/lib)
    link_libraries(-lc++)
endif()

include(GNUInstallDirs)
include(GenerateExportHeader)

if (NOT "$ENV{version}" STREQUAL "")
    set(PROJECT_VERSION "$ENV{version}" CACHE INTERNAL "Copied from environment variable")
endif()

option(LIBCORO_EXTERNAL_DEPENDENCIES "Use Cmake find_package to resolve dependencies instead of embedded libraries, Default=OFF." OFF)
option(LIBCORO_BUILD_TESTS           "Build the tests, Default=ON." ON)
option(LIBCORO_CODE_COVERAGE         "Enable code coverage, tests must also be enabled, Default=OFF" OFF)
option(LIBCORO_BUILD_EXAMPLES        "Build the examples, Default=ON." ON)
option(LIBCORO_RUN_GITCONFIG         "Set the githooks directory to auto format and update the readme, Default=OFF." OFF)
option(LIBCORO_BUILD_SHARED_LIBS     "Build shared libraries, Default=OFF." OFF)

# Set the githooks directory to auto format and update the readme.
if (LIBCORO_RUN_GITCONFIG)
    message("${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR} -> git config --local core.hooksPath .githooks")
    execute_process(
        COMMAND git config --local core.hooksPath .githooks
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

cmake_dependent_option(LIBCORO_FEATURE_NETWORKING "Include networking features, Default=ON." ON "NOT EMSCRIPTEN; NOT MSVC" OFF)
cmake_dependent_option(LIBCORO_FEATURE_TLS "Include TLS encryption features, Default=ON." ON "NOT EMSCRIPTEN; NOT MSVC" OFF)

message("${PROJECT_NAME} LIBCORO_EXTERNAL_DEPENDENCIES = ${LIBCORO_EXTERNAL_DEPENDENCIES}")
message("${PROJECT_NAME} LIBCORO_BUILD_TESTS           = ${LIBCORO_BUILD_TESTS}")
message("${PROJECT_NAME} LIBCORO_CODE_COVERAGE         = ${LIBCORO_CODE_COVERAGE}")
message("${PROJECT_NAME} LIBCORO_BUILD_EXAMPLES        = ${LIBCORO_BUILD_EXAMPLES}")
message("${PROJECT_NAME} LIBCORO_FEATURE_NETWORKING    = ${LIBCORO_FEATURE_NETWORKING}")
message("${PROJECT_NAME} LIBCORO_FEATURE_TLS           = ${LIBCORO_FEATURE_TLS}")
message("${PROJECT_NAME} LIBCORO_RUN_GITCONFIG         = ${LIBCORO_RUN_GITCONFIG}")
message("${PROJECT_NAME} LIBCORO_BUILD_SHARED_LIBS     = ${LIBCORO_BUILD_SHARED_LIBS}")

if(LIBCORO_EXTERNAL_DEPENDENCIES)
    if(LIBCORO_FEATURE_NETWORKING)
        find_package(c-ares CONFIG REQUIRED)
    endif()
else()
    if(NOT LIBCORO_BUILD_TESTS)
        # Disable testing in expected
        set(BUILD_TESTING OFF CACHE INTERNAL "")
    endif()
    if(LIBCORO_FEATURE_NETWORKING)
        set(CARES_STATIC    $<NOT:${LIBCORO_BUILD_SHARED_LIBS}> CACHE INTERNAL "")
        set(CARES_SHARED    ${LIBCORO_BUILD_SHARED_LIBS} CACHE INTERNAL "")
        set(CARES_INSTALL   OFF CACHE INTERNAL "")
        add_subdirectory(vendor/c-ares/c-ares)
    endif()
endif()

set(BUILD_SHARED_LIBS ${LIBCORO_BUILD_SHARED_LIBS} CACHE INTERNAL "")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ${LIBCORO_BUILD_SHARED_LIBS} CACHE INTERNAL "")

set(LIBCORO_SOURCE_FILES
    include/coro/concepts/awaitable.hpp
    include/coro/concepts/buffer.hpp
    include/coro/concepts/executor.hpp
    include/coro/concepts/promise.hpp
    include/coro/concepts/range_of.hpp

    include/coro/detail/task_self_deleting.hpp / src/detail/task_self_deleting.cpp
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
    include/coro/thread_pool.hpp src/thread_pool.cpp
    include/coro/time.hpp
    include/coro/when_all.hpp
    include/coro/when_any.hpp
)

if(LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_SOURCE_FILES
        include/coro/detail/poll_info.hpp

        include/coro/fd.hpp
        include/coro/io_scheduler.hpp src/io_scheduler.cpp
        include/coro/poll.hpp src/poll.cpp
    )

    list(APPEND LIBCORO_SOURCE_FILES
        include/coro/net/dns/resolver.hpp src/net/dns/resolver.cpp
        include/coro/net/connect.hpp src/net/connect.cpp
        include/coro/net/hostname.hpp
        include/coro/net/ip_address.hpp src/net/ip_address.cpp
        include/coro/net/recv_status.hpp src/net/recv_status.cpp
        include/coro/net/send_status.hpp src/net/send_status.cpp
        include/coro/net/socket.hpp src/net/socket.cpp
        include/coro/net/tcp/client.hpp src/net/tcp/client.cpp
        include/coro/net/tcp/server.hpp src/net/tcp/server.cpp
        include/coro/net/udp/peer.hpp src/net/udp/peer.cpp
    )

    if(LIBCORO_FEATURE_TLS)
        find_package(OpenSSL REQUIRED)
        list(APPEND LIBCORO_SOURCE_FILES
            include/coro/net/tls/client.hpp src/net/tls/client.cpp
            include/coro/net/tls/connection_status.hpp src/net/tls/connection_status.cpp
            include/coro/net/tls/context.hpp src/net/tls/context.cpp
            include/coro/net/tls/recv_status.hpp src/net/tls/recv_status.cpp
            include/coro/net/tls/send_status.hpp src/net/tls/send_status.cpp
            include/coro/net/tls/server.hpp src/net/tls/server.cpp
        )
    endif()
endif()

if(DEFINED EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions)
    add_compile_options(-pthread)
    add_compile_options(-matomics)

    add_link_options(-fwasm-exceptions)
    add_link_options(-pthread)
    add_link_options(-sPROXY_TO_PTHREAD)
    add_link_options(-sALLOW_MEMORY_GROWTH)
    add_link_options(-sEXIT_RUNTIME)
endif()

add_library(${PROJECT_NAME} ${LIBCORO_SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX PREFIX "" VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
generate_export_header(${PROJECT_NAME} BASE_NAME CORO EXPORT_FILE_NAME include/coro/export.hpp)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PUBLIC pthread)
endif()

if(LIBCORO_FEATURE_NETWORKING)
    target_link_libraries(${PROJECT_NAME} PUBLIC c-ares::cares)
    target_compile_definitions(${PROJECT_NAME} PUBLIC LIBCORO_FEATURE_NETWORKING)
    if(LIBCORO_FEATURE_TLS)
        target_link_libraries(${PROJECT_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(${PROJECT_NAME} PUBLIC LIBCORO_FEATURE_TLS)
    endif()
endif()

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.2.0")
        message(FATAL_ERROR "g++ version ${CMAKE_CXX_COMPILER_VERSION} is unsupported, please upgrade to at least 10.2.0")
    endif()

    target_compile_options(${PROJECT_NAME} PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>
        $<$<COMPILE_LANGUAGE:CXX>:-fconcepts>
        $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-pipe>
    )
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0.0")
        message(FATAL_ERROR "Clang version ${CMAKE_CXX_COMPILER_VERSION} is unsupported, please upgrade to at least 16.0.0")
    endif()

    target_compile_options(${PROJECT_NAME} PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-pipe>
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC
        /W4
    )
endif()

if(LIBCORO_BUILD_TESTS)
    if(LIBCORO_CODE_COVERAGE)
        target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
        target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
    endif()

    include(CTest)
    add_subdirectory(test)
endif()

if(LIBCORO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()


# generate pc file for pkg-config
if(MSVC)
    set(target1 ${PROJECT_NAME})
else()
    string(REGEX REPLACE "^lib" "" target1 ${PROJECT_NAME})
endif()
configure_file(libcoro.pc.in libcoro.pc @ONLY)

install(TARGETS libcoro)
install(DIRECTORY include/coro TYPE INCLUDE)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/coro TYPE INCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libcoro.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
