# libcoro C++20 coroutine library

[![CI](https://github.com/jbaldwin/libcoro/actions/workflows/ci-coverage.yml/badge.svg)](https://github.com/jbaldwin/libcoro/actions/workflows/ci-coverage.yml)
[![Coverage Status](https://coveralls.io/repos/github/jbaldwin/libcoro/badge.svg?branch=main)](https://coveralls.io/github/jbaldwin/libcoro?branch=main)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/c190d4920e6749d4b4d1a9d7d6687f4f)](https://www.codacy.com/gh/jbaldwin/libcoro/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=jbaldwin/libcoro&amp;utm_campaign=Badge_Grade)
[![language][badge.language]][language]
[![license][badge.license]][license]

**libcoro** is licensed under the Apache 2.0 license.

**libcoro** is meant to provide low level coroutine constructs for building larger applications.

## Overview
* C++20 coroutines!
* Modern Safe C++20 API
* Higher level coroutine constructs
    - [coro::sync_wait(awaitable)](#sync_wait)
    - [coro::when_all(awaitable...) -> awaitable](#when_all)
    - [coro::when_any(awaitable...) -> awaitable](#when_any)
    - [coro::task<T>](#task)
    - [coro::generator<T>](#generator)
    - [coro::event](#event)
    - [coro::latch](#latch)
    - [coro::mutex](#mutex)
    - [coro::shared_mutex](#shared_mutex)
    - [coro::semaphore](#semaphore)
    - [coro::ring_buffer<element, num_elements>](#ring_buffer)
    - [coro::queue](#queue)
    - [coro::condition_variable](#condition_variable)
    - [coro::invoke(functor, args...) -> awaitable](#invoke)
* Executors
    - [coro::thread_pool](#thread_pool) for coroutine cooperative multitasking
    - [coro::scheduler](#scheduler) for driving i/o events
        - Can use `coro::thread_pool` for latency sensitive or long-lived tasks.
        - Can use inline task processing for thread per core or short-lived tasks.
        - Requires `LIBCORO_FEATURE_NETWORKING` to be supported.
    - coro::task_group for grouping dynamic lifetime tasks
* Coroutine Networking
    - coro::net::dns::resolver for async dns
        - Uses libc-ares
    - [coro::net::tcp::client](#scheduler)
    - [coro::net::tcp::server](#scheduler)
      * [Example TCP/HTTP Echo Server](#tcp_echo_server)
    - coro::net::tls::client (OpenSSL)
    - coro::net::tls::server (OpenSSL)
    - coro::net::udp::peer
*
* [Requirements](#requirements)
* [Build Instructions](#build-instructions)
* [Android Support](#android-support)
* [Contributing](#contributing)
* [Support](#support)

## Usage

### A note on co_await and threads
It's important to note with coroutines that _any_ `co_await` has the potential to switch the underlying thread that is executing the currently executing coroutine if the scheduler used has more than 1 thread. In general this shouldn't affect the way any user of the library would write code except for `thread_local`. Usage of `thread_local` should be extremely careful and _never_ used across any `co_await` boundary do to thread switching and work stealing on libcoro's schedulers. The only way this is safe is by using a `coro::thread_pool` with 1 thread or an inline `scheduler` which also only has 1 thread.

### A note on lambda captures
[C++ Core Guidelines - CP.51: Do no use capturing lambdas that are coroutines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture)

The recommendation is to not use lambda captures and instead pass any data into the coroutine via its function arguments by value to guarantee the argument lifetimes. Lambda captures will be destroyed at the coroutines first suspension point so if they are used past that point it will result in a use after free bug.

If you must use lambda captures with your coroutines then libcoro offers [coro::invoke](#invoke) to create a stable coroutine frame to hold the captures for the duration of the user's coroutine.

### sync_wait
The `sync_wait` construct is meant to be used outside a coroutine context to block the calling thread until the coroutine has completed. The coroutine can be executed on the calling thread or scheduled on one of libcoro's schedulers.

```C++
${EXAMPLE_CORO_SYNC_WAIT}
```

Expected output:
```bash
$ ./examples/coro_sync_wait
Inline Result = 10
Offload Result = 20
```

### when_all
The `when_all` construct can be used within coroutines to await a set of tasks, or it can be used outside coroutine context in conjunction with `sync_wait` to await multiple tasks. Each task passed into `when_all` will initially be executed serially by the calling thread so it is recommended to offload the tasks onto an executor like `coro::thread_pool` or `coro::scheduler` so they can execute in parallel.

```C++
${EXAMPLE_CORO_WHEN_ALL}
```

Expected output:
```bash
$ ./examples/coro_when_all
2
4
6
8
10
first: 1.21 second: 20
```

### when_any
The `when_any` construct can be used within coroutines to await a set of tasks and only return the result of the first task that completes. This can also be used outside of a coroutine context in conjunction with `sync_wait` to await the first result. Each task passed into `when_any` will initially be executed serially by the calling thread so it is recommended to offload the tasks onto an executor like `coro::thread_pool` or `coro::scheduler` so they can execute in parallel.

```C++
${EXAMPLE_CORO_WHEN_ANY}
```

Expected output:
```bash
$ ./examples/coro_when_any
result = 1
result = -1
```


### task
The `coro::task<T>` is the main coroutine building block within `libcoro`.  Use task to create your coroutines and `co_await` or `co_yield` tasks within tasks to perform asynchronous operations, lazily evaluation or even spreading work out across a `coro::thread_pool`.  Tasks are lightweight and only begin execution upon awaiting them.


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

### generator
The `coro::generator<T>` construct is a coroutine which can generate one or more values.

```C++
${EXAMPLE_CORO_GENERATOR_CPP}
```

Expected output:
```bash
$ ./examples/coro_generator
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### event
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

### latch
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

### mutex
The `coro::mutex` is a thread safe async tool to protect critical sections and only allow a single thread to execute the critical section at any given time.  Mutexes that are uncontended are a simple CAS operation with a memory fence 'acquire' to behave similar to `std::mutex`.  If the lock is contended then the thread will add itself to a LIFO queue of waiters and yield excution to allow another coroutine to process on that thread while it waits to acquire the lock.

Its important to note that upon releasing the mutex that thread unlocking the mutex will immediately start processing the next waiter in line for the `coro::mutex` (if there are any waiters), the mutex is only unlocked/released once all waiters have been processed.  This guarantees fair execution in a reasonbly FIFO manner, but it also means all coroutines that stack in the waiter queue will end up shifting to the single thread that is executing all waiting coroutines.  It is possible to manually reschedule after the critical section onto a thread pool to re-distribute the work if this is a concern in your use case.

The suspend waiter queue is LIFO, however the worker that current holds the mutex will periodically 'acquire' the current LIFO waiter list to process those waiters when its internal list becomes empty.  This effectively resets the suspended waiter list to empty and the worker holding the mutex will work through the newly acquired LIFO queue of waiters.  It would be possible to reverse this list to be as fair as possible, however not reversing the list should result is better throughput at possibly the cost of some latency for the first suspended waiters on the 'current' LIFO queue.  Reversing the list, however, would introduce latency for all queue waiters since its done everytime the LIFO queue is swapped.

```C++
${EXAMPLE_CORO_MUTEX_CPP}
```

Expected output, note that the output will vary from run to run based on how the thread pool workers
are scheduled and in what order they acquire the mutex lock:
```bash
$ ./examples/coro_mutex
1, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 37, 36, 35, 40, 39, 38, 41, 42, 43, 44, 46, 47, 48, 45, 49, 50, 51, 52, 53, 54, 55, 57, 56, 59, 58, 61, 60, 62, 63, 65, 64, 67, 66, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 83, 82, 84, 85, 86, 87, 88, 89, 91, 90, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

Its very easy to see the LIFO 'atomic' queue in action in the beginning where 22->2 are immediately suspended waiting to acquire the mutex.

### shared_mutex
The `coro::shared_mutex` is a thread safe async tool to allow for multiple shared users at once but also exclusive access.  The lock is acquired strictly in a FIFO manner in that if the lock is currenty held by shared users and an exclusive attempts to lock, the exclusive waiter will suspend until all the _current_ shared users finish using the lock.  Any new users that attempt to lock the mutex in a shared state once there is an exclusive waiter will also wait behind the exclusive waiter.  This prevents the exclusive waiter from being starved.

The `coro::shared_mutex` requires a `executor_type` when constructed to be able to resume multiple shared waiters when an exclusive lock is released.  This allows for all of the pending shared waiters to be resumed concurrently.


```C++
${EXAMPLE_CORO_SHARED_MUTEX_CPP}
```

Example output, notice how the (4,5,6) shared tasks attempt to acquire the lock in a shared state but are blocked behind the exclusive waiter until it completes:
```bash
$ ./examples/coro_shared_mutex
shared task 1 lock_shared()
shared task 1 lock_shared() acquired
shared task 2 lock_shared()
shared task 2 lock_shared() acquired
shared task 3 lock_shared()
shared task 3 lock_shared() acquired
exclusive task lock()
shared task 4 lock_shared()
shared task 5 lock_shared()
shared task 6 lock_shared()
shared task 1 unlock_shared()
shared task 2 unlock_shared()
shared task 3 unlock_shared()
exclusive task lock() acquired
exclusive task unlock()
shared task 4 lock_shared() acquired
shared task 5 lock_shared() acquired
shared task 6 lock_shared() acquired
shared task 4 unlock_shared()
shared task 5 unlock_shared()
shared task 6 unlock_shared()

```

### semaphore
The `coro::semaphore` is a thread safe async tool to protect a limited number of resources by only allowing so many consumers to acquire the resources a single time.  The `coro::semaphore` also has a maximum number of resources denoted by its constructor.  This means if a resource is produced or released when the semaphore is at its maximum resource availability then the release operation will await for space to become available.  This is useful for a ringbuffer type situation where the resources are produced and then consumed, but will have no effect on a semaphores usage if there is a set known quantity of resources to start with and are acquired and then released back.

```C++
${EXAMPLE_CORO_SEMAPHORE_CPP}
```

Expected output, note that there is no lock around the `std::cout` so some of the output isn't perfect.
```bash
$ ./examples/coro_semaphore
1, 23, 25, 24, 22, 27, 28, 29, 21, 20, 19, 18, 17, 14, 31, 30, 33, 32, 41, 40, 37, 39, 38, 36, 35, 34, 43, 46, 47, 48, 45, 42, 44, 26, 16, 15, 13, 52, 54, 55, 53, 49, 51, 57, 58, 50, 62, 63, 61, 60, 59, 56, 12, 11, 8, 10, 9, 7, 6, 5, 4, 3, 642, , 66, 67, 6568, , 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### ring_buffer
The `coro::ring_buffer<element, num_elements>` is thread safe async multi-producer multi-consumer statically sized ring buffer.  Producers that try to produce a value when the ring buffer is full will suspend until space is available.  Consumers that try to consume a value when the ring buffer is empty will suspend until space is available.  All waiters on the ring buffer for producing or consuming are resumed in a LIFO manner when their respective operation becomes available.

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

### queue
The `coro::queue<element_type>` is thread safe async multi-producer multi-consumer queue. Producing into the queue is not an asynchronous operation, it will either immediately use a consumer that is awaiting on `pop()` to process the element, or if no consumer is available place the element into the queue. All consume waiters on the queue are resumed in a LIFO manner when an element becomes available to consume.

```C++
${EXAMPLE_CORO_QUEUE_CPP}
```

Expected output:
```bash
$ ./examples/coro_queue
consumed 0
consumed 1
consumed 0
consumed 2
consumed 3
consumed 4
consumed 1
consumed 0
consumed 0
consumed 0
consumed 1
consumed 1
consumed 2
consumed 2
consumed 3
consumed 4
consumed 3
consumed 4
consumed 2
consumed 3
consumed 4
consumed 1
consumed 2
consumed 3
consumed 4
```

### condition_variable
`coro:condition_variable` allows for tasks to await on the condition until notified with an optional predicate and timeout and stop token. The API for `coro::condition_variable` mostly matches `std::condition_variable`.

NOTE: It is important to *not* hold the `coro::scoped_lock` when calling `notify_one()` or `notify_all()`, this differs from `std::condition_variable` which allows it but doesn't require it. `coro::condition_variable` will deadlock in this scenario based on how coroutines work vs threads.

```C++
${EXAMPLE_CORO_CONDITION_VARIABLE_CPP}
```

Expected output:
```bash
$ ./examples/coro_condition_variable
0 predicate condition = 0               # every call to cv.wait() will invoke the predicate to see if they are ready
1 predicate condition = 0
cv.notify_one() condition = 0           # one waiter is awoken but the predicate is not satisfied
1 predicate condition = 0
cv.notify_one() condition = 1           # one waiter is awoken and the predicate is satisfied
1 predicate condition = 1
1 waiter condition = 1
1 predicate condition = 0
cv.notify_all() condition = 2           # both predicates will pass and both waiters will wake up
1 predicate condition = 2
1 waiter condition = 2
0 predicate condition = 1
0 waiter condition = 1
0 predicate condition = 0
1 predicate condition = 0
cv.notify_all() condition = 1           # one predicate will pass and will wake up
1 predicate condition = 1
1 waiter condition = 1
0 predicate condition = 0
1 predicate condition = 0
ss.request_stop()                       # request to stop, wakeup all waiters and exit
1 predicate condition = 0
1 waiter condition = 0
1 ss.stop_requsted() co_return
0 predicate condition = 0
0 waiter condition = 0
0 ss.stop_requsted() co_return
```

### invoke
`coro::invoke<functor_type, args_types...> -> awaitable` takes a coroutine functor and its arguments, invokes the functor as a coroutine and awaits its result. This is useful for invoking lambda coroutines that have lambdas that need a stable coroutine frame to last the duration of the invocable coroutine.

```C++
${EXAMPLE_CORO_INVOKE_CPP}
```

Example output:
```bash
$ ./examples/coro_invoke
6
```

### thread_pool
`coro::thread_pool` is a statically sized pool of worker threads to execute scheduled coroutines from a FIFO queue.  One way to schedule a coroutine on a thread pool is to use the pool's `schedule()` function which should be `co_awaited` inside the coroutine to transfer the execution from the current thread to a thread pool worker thread.  Its important to note that scheduling will first place the coroutine into the FIFO queue and will be picked up by the first available thread in the pool, e.g. there could be a delay if there is a lot of work queued up.

#### Ways to schedule tasks onto a `coro::thread_pool`
* `coro::thread_pool::schedule()` Use `co_await` on this method inside a coroutine to transfer the tasks execution to the `coro::thread_pool`.
* `coro::thread_pool::schedule(coro::task<T> task) -> coro::task<T>` schedules the task on the `coro::thread_pool` and then returns the result in a task that must be awaited. This is useful if you want to schedule work on the `coro::thread_pool` and want to wait for the result inline.
* `coro::thread_pool::spawn_detached(coro::task<void>&& task)` Spawns the task to be detached and owned by the `coro::thread_pool`, use this if you want to fire and forget the task, the `coro::thread_pool` will maintain the task's lifetime.
* `coro::thread_pool::spawn_joinable(coro::task<void>&& task) -> coro::task<void>` Spawns the task to be started immediately but can be joined at later time, use this if you want to start the task immediately but want to join it later.
* `coro::task_group<coro::thread_pool>(coro::task<void>&& task | range<coro::task<void>)` schedules the task(s) on the `coro::thread_pool`. Use this when you want to share a `coro::thread_pool` while monitoring the progress of a group of tasks.

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

### scheduler
`coro::scheduler` is a i/o event scheduler execution context that can use two methods of task processing:

* A background `coro::thread_pool`
* Inline task processing on the `coro::scheduler`'s event loop

Using a background `coro::thread_pool` will default to using `(std::thread::hardware_concurrency() - 1)` threads to process tasks.  This processing strategy is best for longer tasks that would block the i/o scheduler or for tasks that are latency sensitive.

Using the inline processing strategy will have the event loop i/o thread process the tasks inline on that thread when events are received.  This processing strategy is best for shorter task that will not block the i/o thread for long or for pure throughput by using thread per core architecture, e.g. spin up an inline i/o scheduler per core and inline process tasks on each scheduler.

The `coro::scheduler` can use a dedicated spawned thread for processing events that are ready or it can be maually driven via its `process_events()` function for integration into existing event loops.  By default i/o schedulers will spawn a dedicated event thread and use a thread pool to process tasks.

#### Ways to schedule tasks onto a `coro::scheduler`
* `coro::scheduler::schedule()` Use `co_await` on this method inside a coroutine to transfer the tasks execution to the `coro::scheduler`.
* `coro::scheduler::schedule(coro::task<T> task) -> coro::task<T>` schedules the task on the `coro::scheduler` and then returns the result in a task that must be awaited. This is useful if you want to schedule work on the `coro::scheduler` and want to wait for the result.
* `coro::scheduler::schedule(std::stop_source st, coro::task<T> task, std::chrono::duration timeout) -> coro::expected<T, coro::timeout_status>` schedules the task on the `coro::scheduler` and then returns the result in a task that must be awaited. That task will then either return the completed task's value if it completes before the timeout, or a return value denoted the task timed out. If the task times out the `std::stop_source.request_stop()` will be invoked so the task can check for it and stop executing. This must be done by the user, the `coro::scheduler` cannot stop the execution of the task but it is able through the `std::stop_source` to signal to the task it should stop executing.
* `coro::scheduler::scheduler_after(std::chrono::milliseconds amount)` schedules the current task to be rescheduled after a specified amount of time has passed.
* `coro::scheduler::schedule_at(std::chrono::steady_clock::time_point time)` schedules the current task to be rescheduled at the specified timepoint.
* `coro::scheduler::yield()` will yield execution of the current task and resume after other tasks have had a chance to execute. This effectively places the task at the back of the queue of waiting tasks.
* `coro::scheduler::yield_for(std::chrono::milliseconds amount)` will yield for the given amount of time and then reschedule the task. This is a yield for at least this much time since it is placed in the waiting execution queue and might take additional time to start executing again.
* `coro::scheduler::yield_until(std::chrono::steady_clock::time_point time)` will yield execution until the time point.
* `coro::scheduler::spawn_detached(coro::task<void&& task>)` Spawns the task to be detached and owned by the `coro::scheduler`, use this if you want to fire and forget the task, the `coro::scheduler` will maintain the task's lifetime.
* `coro::scheduler::spawn_joinable(coro::task<void>&& task) -> coro::task<void>` Spawns the task to be started immediately but can be joined at later time, use this if you want to start the task immediately but want to join it later.
* `coro::task_group<coro::scheduler>(coro::task<void>&& task | range<coro::task<void>>)` schedules the task(s) on the `coro::scheduler`. Use this when you want to share a `coro::scheduler` while monitoring the progress of a subset of tasks.

The example provided here shows an i/o scheduler that spins up a basic `coro::net::tcp::server` and a `coro::net::tcp::client` that will connect to each other and then send a request and a response.

```C++
${EXAMPLE_CORO_SCHEDULER_CPP}
```

Example output:
```bash
$ ./examples/coro_scheduler
scheduler::thread_pool worker 0 starting
scheduler::process event thread start
scheduler::thread_pool worker 1 starting
server: Hello from client.
client: Hello from server.
scheduler::thread_pool worker 0 stopping
scheduler::thread_pool worker 1 stopping
scheduler::process event thread stop
```

### tcp_echo_server
See [examples/coro_tcp_echo_erver.cpp](./examples/coro_tcp_echo_server.cpp) for a basic TCP echo server implementation.  You can use tools like `ab` to benchmark against this echo server.

Using a `Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz`:

```bash
$ ab -n 10000000 -c 1000 -k http://127.0.0.1:8888/
This is ApacheBench, Version 2.3 <$Revision: 1879490 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 1000000 requests
Completed 2000000 requests
Completed 3000000 requests
Completed 4000000 requests
Completed 5000000 requests
Completed 6000000 requests
Completed 7000000 requests
Completed 8000000 requests
Completed 9000000 requests
Completed 10000000 requests
Finished 10000000 requests


Server Software:
Server Hostname:        127.0.0.1
Server Port:            8888

Document Path:          /
Document Length:        0 bytes

Concurrency Level:      1000
Time taken for tests:   90.290 seconds
Complete requests:      10000000
Failed requests:        0
Non-2xx responses:      10000000
Keep-Alive requests:    10000000
Total transferred:      1060000000 bytes
HTML transferred:       0 bytes
Requests per second:    110753.80 [#/sec] (mean)
Time per request:       9.029 [ms] (mean)
Time per request:       0.009 [ms] (mean, across all concurrent requests)
Transfer rate:          11464.75 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    0   0.2      0      24
Processing:     2    9   1.6      9      77
Waiting:        0    9   1.6      9      77
Total:          2    9   1.6      9      88

Percentage of the requests served within a certain time (ms)
  50%      9
  66%      9
  75%     10
  80%     10
  90%     11
  95%     12
  98%     14
  99%     15
 100%     88 (longest request)
````

### http_200_ok_server
See [examples/coro_http_200_ok_erver.cpp](./examples/coro_http_200_ok_server.cpp) for a basic HTTP 200 OK response server implementation.  You can use tools like `wrk` or `autocannon` to benchmark against this HTTP 200 OK server.

Using a `Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz`:

```bash
$ ./wrk -c 1000 -d 60s -t 6 http://127.0.0.1:8888/
Running 1m test @ http://127.0.0.1:8888/
  6 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.96ms    3.61ms  82.22ms   90.06%
    Req/Sec    54.69k     6.75k   70.51k    80.88%
  19569177 requests in 1.00m, 1.08GB read
Requests/sec: 325778.99
Transfer/sec:     18.33MB
```

## Android Support

libcoro ships with an Android test harness that builds and runs the library on Android devices and emulators. This is intended to validate coroutine primitives on Android and to sanity-check networking/TLS integration using OpenSSL where available.

### Status at a glance
- Toolchain: Android Gradle Plugin 8.5.2, Gradle Wrapper, NDK r29 (29.0.13846066), CMake 3.22.1
- Minimum SDK: 24, Target SDK: 34
- ABIs: arm64-v8a, armeabi-v7a, x86, x86_64 (APK contains selected ABIs; CI builds per-ABI)
- JNI test app package: `com.example.libcorotest`
- TLS: supported via prebuilt OpenSSL static libraries per-ABI
- Networking: enabled in Android builds (`LIBCORO_FEATURE_NETWORKING`, `LIBCORO_FEATURE_TLS`)

### Project layout
- `test/android/` — Android application project (Gradle)
    - `src/main/cpp/CMakeLists.txt` — integrates libcoro and links in the canonical test sources
    - `src/main/cpp/main.cpp` — JNI host that launches Catch2-based libcoro tests with live log streaming
    - `scripts/build_openssl.sh` — helper to produce per-ABI OpenSSL static libs under `external/openssl/<ABI>/`

### Building the Android test APK locally
Prerequisites: Android SDK + NDK r29, CMake 3.22.1 (installed via SDK), JDK 17.

From repo root:

```bash
cd test/android
# Optionally build OpenSSL for required ABIs (script downloads/compiles):
bash scripts/build_openssl.sh --abis arm64-v8a,armeabi-v7a,x86_64,x86 --api 24

# Single-ABI debug build (faster iterations):
gradle assembleDebug -PciAbi=x86_64 -PcustomBuildDir=build-x86_64

# Multi-ABI build when experimenting locally (produces a fat APK per chosen filters):
gradle assembleDebug
```

Notes:
- You can override the Gradle build directory via `-PcustomBuildDir=...` (used in CI).
- You can restrict to a specific ABI via `-PciAbi=<abi>`.

### Running tests on an emulator
Tests are executed by launching the app, which loads a shared library `libcoroTest.so` that embeds the libcoro test suite (Catch2). Output is streamed to Logcat with tag `coroTest` and also mirrored into the app sandbox file `files/libcoro-tests.log`.

You can pass test options by pushing a simple properties file to the device:

```
filter=~[benchmark] ~[bench] ~[semaphore] ~[scheduler]
timeout=600
```

Place it at `/data/local/tmp/coro_test_config.properties` or inside the app sandbox at `files/coro_test_config.properties`. The JNI runner falls back to defaults when the sandbox is not writable.

Default exclusions in emulator runs skip slow/fragile suites (benchmarks, some networking/TLS servers, long-running schedulers). See `test/android/src/main/cpp/main.cpp` for the current filter set.

### CI pipeline
The GitHub Actions workflow `.github/workflows/ci-android.yml` performs:
- Per-ABI matrix builds (arm64-v8a, armeabi-v7a, x86, x86_64)
- OpenSSL prebuild per ABI via `scripts/build_openssl.sh`
- Emulator provisioning on x86_64 (Android 30), headless launch, storage readiness checks
- Pushing test configuration (filter/timeout) and running the app
- Collecting `coroTest` Logcat into `emulator.log` and exporting `libcoro-tests.log`

The test filter excludes particularly slow suites to keep runs under a 10-minute global timeout inside the app. Adjust `TEST_FILTER`/`TEST_TIMEOUT` env vars in the workflow as needed.

### Known limitations on Android
- Network server tests (e.g., TCP/TLS servers) are skipped in CI to avoid emulator networking flakiness
- Some timing-sensitive `condition_variable` cases may be excluded on emulators due to short timeouts
- The Android harness is for validation; apps should link `libcoro` as a regular CMake target in their own projects

### Using libcoro in your Android CMake project
Add libcoro as a subdirectory in your native CMake and link it to your library target. Example snippet for your module’s CMakeLists:

```cmake
add_subdirectory(${CMAKE_SOURCE_DIR}/path/to/libcoro libcoro_build)
target_link_libraries(your-lib PRIVATE libcoro log)
target_compile_definitions(your-lib PRIVATE LIBCORO_FEATURE_NETWORKING LIBCORO_FEATURE_TLS)
```

If you require TLS, provide OpenSSL for the target ABI (static or shared) and set `OPENSSL_ROOT_DIR`/`OPENSSL_USE_STATIC_LIBS` accordingly.

### Requirements
    C++20 Compiler with coroutine support
        g++ [11, 12, 13]
        clang++ [16, 17]
        MSVC Windows 2022 CL
            Does not currently support:
                `LIBCORO_FEATURE_NETWORKING`
                `LIBCORO_FEATURE_TLS`
    CMake
    make or ninja
    pthreads
    c-ares for async dns (embedded via git submodules)
    openssl for LIBCORO_FEATURE_TLS
    gcov/lcov (For generating coverage only)

### Build Instructions

#### Tested Operating Systems

Full feature supported operating systems:
 * ubuntu:22.04, 24.04
 * fedora:37-43
 * MacOS 15
 * openSUSE/leap:15.6

The following systems currently do not support `LIBCORO_FEATURE_NETWORKING` or `LIBCORO_FEATURE_TLS`:
 * Windows 2022
 * Emscripten 3.1.45

#### Possible issues

##### Linking error

If you encounter a linker error similar to the one below:

```text
_ZN4coro3net3tcp6client9read_some...Frame.destroy referenced in section .rodata.cst8 defined in discarded section .text...Frame.destroy
```

This is a known compiler/linker bug related to coroutine frame destruction.
Update your compiler to the latest version or switch to a modern linker like mold. You can specify the linker in your
CMake configuration:

    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold"

#### Cloning the project
This project uses git submodules, to properly checkout this project use:

    git clone --recurse-submodules <libcoro-url>

This project depends on the following git sub-modules:
 * [libc-ares](https://github.com/c-ares/c-ares) For async DNS resolver, this is a git submodule.
 * [catch2](https://github.com/catchorg/Catch2) For testing, this is embedded in the `test/` directory.

#### Building
    mkdir Release && cd Release
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build .

CMake Options:

| Name                          | Default | Description                                                                                        |
|:------------------------------|:--------|:---------------------------------------------------------------------------------------------------|
| LIBCORO_EXTERNAL_DEPENDENCIES | OFF     | Use CMake find_package to resolve dependencies instead of embedded libraries.                      |
| LIBCORO_BUILD_TESTS           | ON      | Should the tests be built? Note this is only default ON if libcoro is the root CMakeLists.txt      |
| LIBCORO_CODE_COVERAGE         | OFF     | Should code coverage be enabled? Requires tests to be enabled.                                     |
| LIBCORO_BUILD_EXAMPLES        | ON      | Should the examples be built? Note this is only default ON if libcoro is the root CMakeLists.txt   |
| LIBCORO_FEATURE_NETWORKING    | ON      | Include networking features. MSVC not currently supported                                          |
| LIBCORO_FEATURE_TLS           | ON      | Include TLS features. Requires networking to be enabled. MSVC not currently supported.             |

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

##### Package managers
libcoro is available via package managers [Conan](https://conan.io/center/libcoro) and [vcpkg](https://vcpkg.io/).

### Contributing

Contributing is welcome, if you have ideas or bugs please open an issue. If you want to open a PR they are also welcome, if you are adding a bugfix or a feature please include tests to verify the feature or bugfix is working properly. If it isn't included I will be asking for you to add some!

#### Tests
The tests will automatically be run by github actions on creating a pull request. They can also be ran locally after building from the build directory:

    # Invoke via cmake with all output from the tests displayed to console:
    ctest -VV

    # Or invoke directly, can pass the name of tests to execute, the framework used is catch2.
    # Tests are tagged with their group, below is how to run all of the coro::net::tcp::server tests:
    ./test/libcoro_test "[tcp_server]"

If you open a PR for a bugfix or new feature please include tests to verify that the change is working as intended. If your PR doesn't include tests I will ask you to add them and won't merge until they are added and working properly. Tests are found in the `/test` directory and are organized by object type.

### Support

File bug reports, feature requests and questions using [GitHub libcoro Issues](https://github.com/jbaldwin/libcoro/issues)

Copyright © 2020-2025 Josh Baldwin

[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg
[badge.license]: https://img.shields.io/badge/license-Apache--2.0-blue

[language]: https://en.wikipedia.org/wiki/C%2B%2B17
[license]: https://en.wikipedia.org/wiki/Apache_License
