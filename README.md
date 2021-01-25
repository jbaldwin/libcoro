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
    - coro::task<T>
    - coro::generator<T>
    - coro::event
    - coro::latch
    - coro::mutex
    - coro::sync_wait(awaitable)
        - coro::when_all_awaitabe(awaitable...) -> coro::task<T>...
        - coro::when_all(awaitable...) -> T... (Future)
* Schedulers
    - coro::thread_pool for coroutine cooperative multitasking
    - coro::io_scheduler for driving i/o events, uses thread_pool for coroutine execution
        - epoll driver
        - io_uring driver (Future, will be required for async file i/o)
* Coroutine Networking
    - coro::net::dns_resolver for async dns, leverages libc-ares
    - coro::net::tcp_client and coro::net::tcp_server
    - coro::net::udp_peer

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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    coro::event e;

    // These tasks will wait until the given event has been set before advancing.
    auto make_wait_task = [](const coro::event& e, uint64_t i) -> coro::task<void> {
        std::cout << "task " << i << " is waiting on the event...\n";
        co_await e;
        std::cout << "task " << i << " event triggered, now resuming.\n";
        co_return;
    };

    // This task will trigger the event allowing all waiting tasks to proceed.
    auto make_set_task = [](coro::event& e) -> coro::task<void> {
        std::cout << "set task is triggering the event\n";
        e.set();
        co_return;
    };

    // Synchronously wait until all the tasks are completed, this is intentionally
    // starting the first 3 wait tasks prior to the final set task so the waiters suspend
    // their coroutine before being resumed.
    coro::sync_wait(
        coro::when_all_awaitable(make_wait_task(e, 1), make_wait_task(e, 2), make_wait_task(e, 3), make_set_task(e)));
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // This task will wait until the given latch setters have completed.
    auto make_latch_task = [](coro::latch& l) -> coro::task<void> {
        std::cout << "latch task is now waiting on all children tasks...\n";
        co_await l;
        std::cout << "latch task children tasks completed, resuming.\n";
        co_return;
    };

    // This task does 'work' and counts down on the latch when completed.  The final child task to
    // complete will end up resuming the latch task when the latch's count reaches zero.
    auto make_worker_task = [](coro::latch& l, int64_t i) -> coro::task<void> {
        std::cout << "work task " << i << " is working...\n";
        std::cout << "work task " << i << " is done, counting down on the latch\n";
        l.count_down();
        co_return;
    };

    // It is important to note that the latch task must not 'own' the worker tasks within its
    // coroutine stack frame because the final worker task thread will execute the latch task upon
    // setting the latch counter to zero.  This means that:
    //     1) final worker task calls count_down() => 0
    //     2) resume execution of latch task to its next suspend point or completion, IF completed
    //        then this coroutine's stack frame is destroyed!
    //     3) final worker task continues exection
    // If the latch task 'own's the worker task objects then they will destruct prior to step (3)
    // if the latch task completes on that resume, and it will be attempting to execute an already
    // destructed coroutine frame.
    // This example correctly has the latch task and all its waiting tasks on the same scope/frame
    // to avoid this issue.
    const int64_t                 num_tasks{5};
    coro::latch                   l{num_tasks};
    std::vector<coro::task<void>> tasks{};

    // Make the latch task first so it correctly waits for all worker tasks to count down.
    tasks.emplace_back(make_latch_task(l));
    for (int64_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_worker_task(l, i));
    }

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all_awaitable(tasks));
}
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
 * [catch2](https://github.com/catchorg/Catch2) For testing.

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

    # Invoke via cmake with all output from the tests displayed to console:
    ctest -VV

    # Or invoke directly, can pass the name of tests to execute, the framework used is catch2.
    # Tests are tagged with their group, below is howt o run all of the coro::net::tcp_server tests:
    ./Debug/test/libcoro_test "[tcp_server]"

### Support

File bug reports, feature requests and questions using [GitHub libcoro Issues](https://github.com/jbaldwin/libcoro/issues)

Copyright © 2020-2021 Josh Baldwin

[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg
[badge.license]: https://img.shields.io/badge/license-Apache--2.0-blue

[language]: https://en.wikipedia.org/wiki/C%2B%2B17
[license]: https://en.wikipedia.org/wiki/Apache_License
