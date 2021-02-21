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
    - coro::when_all(awaitable...) -> awaitable
* Schedulers
    - coro::thread_pool for coroutine cooperative multitasking
    - coro::io_scheduler for driving i/o events, uses thread_pool for coroutine execution upon triggered events
        - epoll driver
        - io_uring driver (Future, will be required for async file i/o)
* Coroutine Networking
    - coro::net::dns_resolver for async dns
        - Uses libc-ares
    - coro::net::tcp_client
        - Supports SSL/TLS via OpenSSL
    - coro::net::tcp_server
        - Supports SSL/TLS via OpenSSL
    - coro::net::udp_peer

## Usage

### A note on co_await
Its important to note with coroutines that depending on the construct used _any_ `co_await` has the potential to switch the thread that is executing the currently running coroutine.  In general this shouldn't affect the way any user of the library would write code except for `thread_local`.  Usage of `thread_local` should be extremely careful and _never_ used across any `co_await` boundary do to thread switching and work stealing on thread pools.

### coro::task<T>
The `coro::task<T>` is the main coroutine building block within `libcoro`.  Use task to create your coroutines and `co_await` or `co_yield` tasks within tasks to perform asynchronous operations, lazily evaluation or even spreading work out across a `coro::thread_pool`.  Tasks are lightweight and only begin execution upon awaiting them.  If their return type is not `void` then the value can be returned by const reference or by moving (r-value reference).


```C++
${EXAMPLE_CORO_TASK_CPP}
```

Expected output:
```bash
$ ./examples/coro_task
Task1 output = 9
expensive_struct() move constructor called
expensive_struct() move assignment called
expensive_struct() move constructor called
12345678-1234-5678-9012-123456781234 has 90000 records.
Answer to everything = 42
```

### coro::generator<T>
The `coro::generator<T>` construct is a coroutine which can generate one or more values.

```C++
${EXAMPLE_CORO_GENERATOR_CPP}
```

Expected output:
```bash
$ ./examples/coro_generator
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### coro::event
The `coro::event` is a thread safe async tool to have 1 or more waiters suspend for an event to be set before proceeding.  The implementation of event currently will resume execution of all waiters on the thread that sets the event.  If the event is already set when a waiter goes to wait on the thread they will simply continue executing with no suspend or wait time incurred.

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
The `coro::latch` is a thread safe async tool to have 1 waiter suspend until all outstanding events have completed before proceeding.

```C++
${EXAMPLE_CORO_LATCH_CPP}
```

Expected output:
```bash
$ ./examples/coro_latch
latch task is now waiting on all children tasks...
worker task 1 is working...
worker task 2 is working...
worker task 3 is working...
worker task 4 is working...
worker task 5 is working...
worker task 1 is done, counting down on the latch
worker task 2 is done, counting down on the latch
worker task 3 is done, counting down on the latch
worker task 4 is done, counting down on the latch
worker task 5 is done, counting down on the latch
latch task dependency tasks completed, resuming.
```

### coro::mutex
The `coro::mutex` is a thread safe async tool to protect critical sections and only allow a single thread to execute the critical section at any given time.  Mutexes that are uncontended are a simple CAS operation with a memory fence 'acquire' to behave similar to `std::mutex`.  If the lock is contended then the thread will add itself to a FIFO queue of waiters and yield excution to allow another coroutine to process on that thread while it waits to acquire the lock.

Its important to note that upon releasing the mutex that thread will immediately start processing the next waiter in line for the `coro::mutex`, the mutex is only unlocked/released once all waiters have been processed.  This guarantees fair execution in a FIFO manner, but it also means all coroutines that stack in the waiter queue will end up shifting to the single thread that is executing all waiting coroutines.  It is possible to reschedule after the critical section onto a thread pool to re-distribute the work.  Perhaps an auto-reschedule on a given thread pool is a good feature to implement in the future to prevent this behavior so the post critical section work in the coroutines is redistributed amongst all available thread pool threads.

```C++
${EXAMPLE_CORO_MUTEX_CPP}
```

Expected output, note that the output will vary from run to run based on how the thread pool workers
are scheduled and in what order they acquire the mutex lock:
```bash
$ ./examples/coro_mutex
1, 2, 3, 4, 5, 6, 7, 8, 10, 9, 12, 11, 13, 14, 15, 16, 17, 18, 19, 21, 22, 20, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 47, 48, 49, 46, 50, 51, 52, 53, 54, 55, 57, 58, 59, 56, 60, 62, 61, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### coro::semaphore
The `coro::semaphore` is a thread safe async tool to protect a limited number of resources by only allowing so many consumers to acquire the resources a single time.  The `coro::semaphore` also has a maximum number of resources denoted by its constructor.  This means if a resource is produced or released when the semaphore is at its maximum resource availability then the release operation will await for space to become available.  This is useful for a ringbuffer type situation where the resources are produced and then consumed, but will have no effect on a semaphores usage if there is a set known quantity of resources to start with and are acquired and then released back.

```C++
${EXAMPLE_CORO_SEMAPHORE_CPP}
```

Expected output, note that there is no lock around the `std::cout` so some of the output isn't perfect.
```bash
$ ./examples/coro_semaphore
1, 23, 25, 24, 22, 27, 28, 29, 21, 20, 19, 18, 17, 14, 31, 30, 33, 32, 41, 40, 37, 39, 38, 36, 35, 34, 43, 46, 47, 48, 45, 42, 44, 26, 16, 15, 13, 52, 54, 55, 53, 49, 51, 57, 58, 50, 62, 63, 61, 60, 59, 56, 12, 11, 8, 10, 9, 7, 6, 5, 4, 3, 642, , 66, 67, 6568, , 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### coro::ring_buffer<element, num_elements>
The `coro::ring_buffer` is thread safe async multi-producer multi-consumer statically sized ring buffer.  Producers will that try to produce a value when the ring buffer is full will suspend until space is available.  Consumers that try to consume a value when the ring buffer is empty will suspend until space is available.  All waiters on the ring buffer for producing or consuming are resumed in a LIFO manner when their respective operation becomes available.

The `coro::ring_buffer` also works with `coro::stop_signal` in that if the ring buffers `stop_

```C++
${EXAMPLE_CORO_RING_BUFFER_CPP}
```

Expected output:
```bash
$ ./examples/coro_ring_buffer
(id=3, v=1), (id=2, v=2), (id=1, v=3), (id=0, v=4), (id=3, v=5), (id=2, v=6), (id=1, v=7), (id=0, v=8), (id=3, v=9), (id=2, v=10), (id=1, v=11), (id=0, v=12), (id=3, v=13), (id=2, v=14), (id=1, v=15), (id=0, v=16), (id=3, v=17), (id=2, v=18), (id=1, v=19), (id=0, v=20), (id=3, v=21), (id=2, v=22), (id=1, v=23), (id=0, v=24), (id=3, v=25), (id=2, v=26), (id=1, v=27), (id=0, v=28), (id=3, v=29), (id=2, v=30), (id=1, v=31), (id=0, v=32), (id=3, v=33), (id=2, v=34), (id=1, v=35), (id=0, v=36), (id=3, v=37), (id=2, v=38), (id=1, v=39), (id=0, v=40), (id=3, v=41), (id=2, v=42), (id=0, v=44), (id=1, v=43), (id=3, v=45), (id=2, v=46), (id=0, v=47), (id=3, v=48), (id=2, v=49), (id=0, v=50), (id=3, v=51), (id=2, v=52), (id=0, v=53), (id=3, v=54), (id=2, v=55), (id=0, v=56), (id=3, v=57), (id=2, v=58), (id=0, v=59), (id=3, v=60), (id=1, v=61), (id=2, v=62), (id=0, v=63), (id=3, v=64), (id=1, v=65), (id=2, v=66), (id=0, v=67), (id=3, v=68), (id=1, v=69), (id=2, v=70), (id=0, v=71), (id=3, v=72), (id=1, v=73), (id=2, v=74), (id=0, v=75), (id=3, v=76), (id=1, v=77), (id=2, v=78), (id=0, v=79), (id=3, v=80), (id=2, v=81), (id=1, v=82), (id=0, v=83), (id=3, v=84), (id=2, v=85), (id=1, v=86), (id=0, v=87), (id=3, v=88), (id=2, v=89), (id=1, v=90), (id=0, v=91), (id=3, v=92), (id=2, v=93), (id=1, v=94), (id=0, v=95), (id=3, v=96), (id=2, v=97), (id=1, v=98), (id=0, v=99), (id=3, v=100),
producer is sending stop signal
consumer 0 shutting down, stop signal received
consumer 1 shutting down, stop signal received
consumer 2 shutting down, stop signal received
consumer 3 shutting down, stop signal received
```

### coro::thread_pool
`coro::thread_pool` is a statically sized pool of worker threads to execute scheduled coroutines from a FIFO queue.  To schedule a coroutine on a thread pool the pool's `schedule()` function should be `co_awaited` to transfer the execution from the current thread to a thread pool worker thread.  Its important to note that scheduling will first place the coroutine into the FIFO queue and will be picked up by the first available thread in the pool, e.g. there could be a delay if there is a lot of work queued up.

```C++
${EXAMPLE_CORO_THREAD_POOL_CPP}
```

Example output (will vary based on threads):
```bash
$ ./examples/coro_thread_pool
thread pool worker 0 is starting up.
thread pool worker 2 is starting up.
thread pool worker 3 is starting up.
thread pool worker 1 is starting up.
Task 2 is yielding()
Task 3 is yielding()
Task 0 is yielding()
Task 1 is yielding()
Task 4 is yielding()
Task 5 is yielding()
Task 6 is yielding()
Task 7 is yielding()
Task 8 is yielding()
Task 9 is yielding()
calculated thread pool result = 4999898
thread pool worker 1 is shutting down.
thread pool worker 2 is shutting down.
thread pool worker 3 is shutting down.
thread pool worker 0 is shutting down.
```

### coro::io_scheduler
`coro::io_scheduler` is a i/o event scheduler that uses a statically sized pool (`coro::thread_pool`) to process the events that are ready.  The `coro::io_scheduler` can use a dedicated spawned thread for processing events that are ready or it can be maually driven via its `process_events()` function for integration into existing event loops.  If using the dedicated thread to process i/o events the dedicated thread does not execute and of the tasks itself, it simply schedules them to be executed on the next availble worker thread in its embedded `coro::thread_pool`.  Inline execution of tasks on the i/o dedicated thread is not supported since it can introduce poor latency when an expensive task is executing.

The example provided here shows an i/o scheduler that spins up a basic `coro::net::tcp_server` and a `coro::net::tcp_client` that will connect to each other and then send a request and a response.

```C++
${EXAMPLE_CORO_IO_SCHEDULER_CPP}
```

Example output:
```bash
$ ./examples/coro_io_scheduler
io_scheduler::thread_pool worker 0 starting
io_scheduler::process event thread start
io_scheduler::thread_pool worker 1 starting
server: Hello from client.
client: Hello from server.
io_scheduler::thread_pool worker 0 stopping
io_scheduler::thread_pool worker 1 stopping
io_scheduler::process event thread stop
```

### coro::task_container
`coro::task_container` is a special container type that will maintain the lifetime of tasks that do not have a known lifetime.  This is extremely useful for tasks that hold open connections to clients and possibly process multiple requests from that client before shutting down.  The task doesn't know how long it will be alive but at some point in the future it will complete and need to have its resources cleaned up.  The `coro::task_container` does this by wrapping the users task into anothe coroutine task that will mark itself for deletion upon completing within the parent task container.  The task container should then run garbage collection periodically, or by default when a new task is added, to prune completed tasks from the container.

All tasks that are stored within a `coro::task_container` must have a `void` return type since their result cannot be accessed due to the task's lifetime being indeterminate.

```C++
${EXAMPLE_CORO_TASK_CONTAINER_CPP}
```

```bash
$ ./examples/coro_task_container
server: Hello from client 1
client: Hello from server 1
server: Hello from client 2
client: Hello from server 2
server: Hello from client 3
client: Hello from server 3
server: Hello from client 4
client: Hello from server 4
server: Hello from client 5
client: Hello from server 5
```

### Requirements
    C++20 Compiler with coroutine support
        g++10.2 is tested
    CMake
    make or ninja
    pthreads
    openssl
    gcov/lcov (For generating coverage only)

### Instructions

#### Cloning the project
This project uses gitsubmodules, to properly checkout this project use:

    git clone --recurse-submodules <libcoro-url>

This project depends on the following git sub-modules:
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
