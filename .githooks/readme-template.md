# libcoro C++20 linux coroutine library

[![CI](https://github.com/jbaldwin/libcoro/workflows/build/badge.svg)](https://github.com/jbaldwin/libcoro/workflows/build/badge.svg)
[![Coverage Status](https://coveralls.io/repos/github/jbaldwin/libcoro/badge.svg?branch=master)](https://coveralls.io/github/jbaldwin/libcoro?branch=master)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/c190d4920e6749d4b4d1a9d7d6687f4f)](https://www.codacy.com/gh/jbaldwin/libcoro/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=jbaldwin/libcoro&amp;utm_campaign=Badge_Grade)
[![language][badge.language]][language]
[![license][badge.license]][license]

**libcoro** is licensed under the Apache 2.0 license.

**libcoro** is meant to provide low level coroutine constructs for building larger applications, the current focus is around high performance networking coroutine support.

## Overview
* C++20 coroutines!
* Modern Safe C++20 API
* Higher level coroutine constructs
** coro::task<T>
** coro::generator<T>
** coro::event
** coro::latch
** coro::mutex
** coro::sync_wait(awaitable)
*** coro::when_all(awaitable...)
* Schedulers
** coro::thread_pool for coroutine cooperative multitasking
** coro::io_scheduler for driving i/o events, uses thread_pool
*** epoll driver implemented
*** io_uring driver planned (will be required for file i/o)
* Coroutine Networking
** coro::net::dns_resolver for async dns, leverages libc-ares
 ** coro::tcp_client and coro::tcp_server
** coro::udp_peer

### A note on co_await
Its important to note with coroutines that depending on the construct used _any_ `co_await` has the
potential to switch the thread that is executing the currently running coroutine.  In general this shouldn't
affect the way any user of the library would write code except for `thread_local`.  Usage of `thread_local`
should be extremely careful and _never_ used across any `co_await` boundary do to thread switching and
work stealing on thread pools.

### coro::event
The `coro::event` is a thread safe async tool to have 1 or more waiters suspend for an event to be set
before proceeding.  The implementation of event currently will resume execution of all waiters on the
thread that sets the event.  If the event is already set when a waiter goes to wait on the thread they
will simply continue executing with no suspend or wait time incurred.

```C++
${EXAMPLE_CORO_EVENT_CPP}
```

Expected output:
```bash
$ ./examples/coro_event
task 1 is waiting on the event...
task 2 is waiting on the event...
task 3 is waiting on the event...
set task is triggering the event
task 3 event triggered, now resuming.
task 2 event triggered, now resuming.
task 1 event triggered, now resuming.
```

### coro::latch
The `coro::latch` is a thread safe async tool to have 1 waiter suspend until all outstanding events
have completed before proceeding.

```C++
${EXAMPLE_CORO_LATCH_CPP}
```

Expected output:
```bash
$ ./examples/coro_latch
latch task is now waiting on all children tasks...
work task 1 is working...
work task 1 is done, counting down on the latch
work task 2 is working...
work task 2 is done, counting down on the latch
work task 3 is working...
work task 3 is done, counting down on the latch
work task 4 is working...
work task 4 is done, counting down on the latch
work task 5 is working...
work task 5 is done, counting down on the latch
latch task children tasks completed, resuming.
```

## Usage

### Requirements
    C++20 Compiler with coroutine support
        g++10.2 is tested
    CMake
    make or ninja
    pthreads
    gcov/lcov (For generating coverage only)

### Instructions

#### Cloning the project
This project uses gitsubmodules, to properly checkout this project use:

    git clone --recurse-submodules <libcoro-url>

This project depends on the following projects:
 * [libc-ares](https://github.com/c-ares/c-ares) For async DNS resolver.

#### Building
    mkdir Release && cd Release
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build .

CMake Options:

| Name                   | Default | Description                                                   |
|:-----------------------|:--------|:--------------------------------------------------------------|
| LIBCORO_BUILD_TESTS    | ON      | Should the tests be built?                                    |
| LIBCORO_CODE_COVERAGE  | OFF     | Should code coverage be enabled? Requires tests to be enabled |
| LIBCORO_BUILD_EXAMPLES | ON      | Should the examples be built?                                 |

#### Adding to your project

##### add_subdirectory()

```cmake
# Include the checked out libcoro code in your CMakeLists.txt file
add_subdirectory(path/to/libcoro)

# Link the libcoro cmake target to your project(s).
target_link_libraries(${PROJECT_NAME} PUBLIC libcoro)

```

##### FetchContent
CMake can include the project directly by downloading the source, compiling and linking to your project via FetchContent, below is an example on how you might do this within your project.


```cmake
cmake_minimum_required(VERSION 3.11)

# Fetch the project and make it available for use.
include(FetchContent)
FetchContent_Declare(
    libcoro
    GIT_REPOSITORY https://github.com/jbaldwin/libcoro.git
    GIT_TAG        <TAG_OR_GIT_HASH>
)
FetchContent_MakeAvailable(libcoro)

# Link the libcoro cmake target to your project(s).
target_link_libraries(${PROJECT_NAME} PUBLIC libcoro)

```

#### Tests
The tests will automatically be run by github actions on creating a pull request.  They can also be ran locally:

    # Invoke via cmake:
    ctest -VV

    # Or invoke directly, can pass the name of tests to execute, the framework used is catch2
    # catch2 supports '*' wildcards to run multiple tests or comma delimited ',' test names.
    # The below will run all tests with "tcp_server" prefix in their test name.
    ./Debug/test/libcoro_test "tcp_server*"

### Support

File bug reports, feature requests and questions using [GitHub libcoro Issues](https://github.com/jbaldwin/libcoro/issues)

Copyright © 2020-2021 Josh Baldwin

[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg
[badge.license]: https://img.shields.io/badge/license-Apache--2.0-blue

[language]: https://en.wikipedia.org/wiki/C%2B%2B17
[license]: https://en.wikipedia.org/wiki/Apache_License
