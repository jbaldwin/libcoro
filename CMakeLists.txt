cmake_minimum_required(VERSION 3.15)
include(CMakeDependentOption)
find_package(Git)

# Determine version from `git describe` when git is available and a matching tag exists.
set(PROJECT_VERSION "0.0.0")
set(GIT_VERSION "")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --match v[0-9]* --always --tags --dirty=-dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
        OUTPUT_VARIABLE GIT_VERSION
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(GIT_DESCRIBE_RESULT EQUAL 0)
    # Expected forms: vN.N.N, vN.N.N-..., vN.N.N-dirty
        if(GIT_VERSION MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)")
            string(REGEX REPLACE "^v([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" PROJECT_VERSION "${GIT_VERSION}")
        endif()
    endif()
endif()

message(STATUS "git describe last release tag: ${GIT_VERSION}")

project(libcoro
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
    DESCRIPTION "C++20 coroutine library"
)

message(STATUS "${PROJECT_NAME} version: ${PROJECT_VERSION}")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
    # We need to build with clang >= 17, assuming its installed by
    # brew. We also need to force linking to libc++.a
    add_link_options(-L/usr/local/opt/llvm/lib)
    link_libraries(-lc++)
endif()

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

include(GNUInstallDirs)
include(GenerateExportHeader)

if (NOT "$ENV{version}" STREQUAL "")
    set(PROJECT_VERSION "$ENV{version}" CACHE INTERNAL "Copied from environment variable")
endif()

option(LIBCORO_ENABLE_ASAN           "Build with address sanitizer")
option(LIBCORO_ENABLE_MSAN           "Build with memory sanitizer")
option(LIBCORO_ENABLE_TSAN           "Build with thread sanitizer")
option(LIBCORO_ENABLE_USAN           "Build with undefined sanitizer")
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

message(STATUS "LIBCORO_ENABLE_ASAN           = ${LIBCORO_ENABLE_ASAN}")
message(STATUS "LIBCORO_ENABLE_MSAN           = ${LIBCORO_ENABLE_MSAN}")
message(STATUS "LIBCORO_ENABLE_TSAN           = ${LIBCORO_ENABLE_TSAN}")
message(STATUS "LIBCORO_ENABLE_USAN           = ${LIBCORO_ENABLE_USAN}")
message(STATUS "LIBCORO_EXTERNAL_DEPENDENCIES = ${LIBCORO_EXTERNAL_DEPENDENCIES}")
message(STATUS "LIBCORO_BUILD_TESTS           = ${LIBCORO_BUILD_TESTS}")
message(STATUS "LIBCORO_CODE_COVERAGE         = ${LIBCORO_CODE_COVERAGE}")
message(STATUS "LIBCORO_BUILD_EXAMPLES        = ${LIBCORO_BUILD_EXAMPLES}")
message(STATUS "LIBCORO_FEATURE_NETWORKING    = ${LIBCORO_FEATURE_NETWORKING}")
message(STATUS "LIBCORO_FEATURE_TLS           = ${LIBCORO_FEATURE_TLS}")
message(STATUS "LIBCORO_RUN_GITCONFIG         = ${LIBCORO_RUN_GITCONFIG}")
message(STATUS "LIBCORO_BUILD_SHARED_LIBS     = ${LIBCORO_BUILD_SHARED_LIBS}")

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
        target_compile_options(c-ares PRIVATE -w)
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

    include/coro/detail/awaiter_list.hpp
    include/coro/detail/task_self_deleting.hpp src/detail/task_self_deleting.cpp
    include/coro/detail/void_value.hpp

    include/coro/attribute.hpp
    include/coro/condition_variable.hpp src/condition_variable.cpp
    include/coro/coro.hpp
    include/coro/default_executor.hpp src/default_executor.cpp
    include/coro/event.hpp src/event.cpp
    include/coro/generator.hpp
    include/coro/invoke.hpp
    include/coro/latch.hpp
    include/coro/mutex.hpp src/mutex.cpp
    include/coro/queue.hpp
    include/coro/ring_buffer.hpp
    include/coro/semaphore.hpp src/semaphore.cpp
    include/coro/shared_mutex.hpp
    include/coro/sync_wait.hpp src/sync_wait.cpp
    include/coro/task.hpp
    include/coro/task_group.hpp
    include/coro/thread_pool.hpp src/thread_pool.cpp
    include/coro/time.hpp
    include/coro/when_all.hpp
    include/coro/when_any.hpp
)

if(LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_SOURCE_FILES
        include/coro/detail/pipe.hpp src/detail/pipe.cpp
        include/coro/detail/poll_info.hpp
        include/coro/detail/timer_handle.hpp src/detail/timer_handle.cpp

        include/coro/fd.hpp
        include/coro/scheduler.hpp src/scheduler.cpp
        include/coro/io_notifier.hpp
        include/coro/poll.hpp src/poll.cpp
    )

    if(LINUX)
        list(APPEND LIBCORO_SOURCE_FILES
            include/coro/detail/io_notifier_epoll.hpp src/detail/io_notifier_epoll.cpp
        )
    endif()
    if(MACOSX)
        list(APPEND LIBCORO_SOURCE_FILES
            include/coro/detail/io_notifier_kqueue.hpp src/detail/io_notifier_kqueue.cpp
        )
    endif()


    list(APPEND LIBCORO_SOURCE_FILES
        include/coro/net/dns/resolver.hpp src/net/dns/resolver.cpp
        include/coro/net/connect.hpp src/net/connect.cpp
        include/coro/net/hostname.hpp
        include/coro/net/ip_address.hpp src/net/ip_address.cpp
        include/coro/net/socket_address.hpp
        include/coro/net/io_status.hpp src/net/io_status.cpp
        include/coro/net/socket.hpp src/net/socket.cpp
        include/coro/net/tcp/client.hpp src/net/tcp/client.cpp
        include/coro/net/tcp/server.hpp src/net/tcp/server.cpp
        include/coro/net/udp/peer.hpp src/net/udp/peer.cpp
    )

    if(LIBCORO_FEATURE_TLS)
        # Android embedding: if caller already defined imported targets OpenSSL::SSL and OpenSSL::Crypto
        # (e.g. prebuilt static libs), skip find_package to avoid accidentally picking host libs.
        if (NOT (TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto))
            find_package(OpenSSL REQUIRED)
        endif()
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

if(LIBCORO_ENABLE_ASAN)
    add_compile_options(-g -O0 -fno-omit-frame-pointer -fsanitize=address)
    add_link_options( -fsanitize=address)
endif()

if(LIBCORO_ENABLE_MSAN)
    add_compile_options(-g -O0 -fno-omit-frame-pointer -fsanitize=memory)
    add_link_options( -fsanitize=memory)
endif()

if(LIBCORO_ENABLE_TSAN)
    add_compile_options(-g -O0 -fno-omit-frame-pointer -fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

if(LIBCORO_ENABLE_USAN)
    add_compile_options(-g -O0 -fno-omit-frame-pointer -fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
generate_export_header(${PROJECT_NAME} BASE_NAME CORO EXPORT_FILE_NAME include/coro/export.hpp)

if(UNIX AND NOT ANDROID)
    # On Android, pthread is built into libc; there is no separate libpthread, so using -lpthread results in an error.
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
