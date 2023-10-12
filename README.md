# libcoro C++20 linux coroutine library

[![CI](https://github.com/jbaldwin/libcoro/workflows/build/badge.svg)](https://github.com/jbaldwin/libcoro/workflows/build/badge.svg)
[![Coverage Status](https://coveralls.io/repos/github/jbaldwin/libcoro/badge.svg?branch=issue-1/ci)](https://coveralls.io/github/jbaldwin/libcoro?branch=issue-1/ci)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/c190d4920e6749d4b4d1a9d7d6687f4f)](https://www.codacy.com/gh/jbaldwin/libcoro/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=jbaldwin/libcoro&amp;utm_campaign=Badge_Grade)
[![language][badge.language]][language]
[![license][badge.license]][license]

**libcoro** is licensed under the Apache 2.0 license.

**libcoro** is meant to provide low level coroutine constructs for building larger applications, the current focus is around high performance networking coroutine support.

## Overview
* C++20 coroutines!
* Modern Safe C++20 API
* Higher level coroutine constructs
    - [coro::task<T>](#task)
    - [coro::generator<T>](#generator)
    - [coro::event](#event)
    - [coro::latch](#latch)
    - [coro::mutex](#mutex)
    - [coro::shared_mutex](#shared_mutex)
    - [coro::semaphore](#semaphore)
    - [coro::ring_buffer<element, num_elements>](#ring_buffer)
    - coro::sync_wait(awaitable)
    - coro::when_all(awaitable...) -> awaitable
* Schedulers
    - [coro::thread_pool](#thread_pool) for coroutine cooperative multitasking
    - [coro::io_scheduler](#io_scheduler) for driving i/o events
        - Can use coro::thread_pool for latency sensitive or long lived tasks.
        - Can use inline task processing for thread per core or short lived tasks.
        - Currently uses an epoll driver
    - [coro::task_container](#task_container) for dynamic task lifetimes
* Coroutine Networking
    - coro::net::dns_resolver for async dns
        - Uses libc-ares
    - [coro::net::tcp_client](#io_scheduler)
        - Supports SSL/TLS via OpenSSL
    - [coro::net::tcp_server](#io_scheduler)
        - Supports SSL/TLS via OpenSSL
    - coro::net::udp_peer
* [Example TCP/HTTP Echo Server](#tcp_echo_server)

## Usage

### A note on co_await
Its important to note with coroutines that depending on the construct used _any_ `co_await` has the potential to switch the thread that is executing the currently running coroutine.  In general this shouldn't affect the way any user of the library would write code except for `thread_local`.  Usage of `thread_local` should be extremely careful and _never_ used across any `co_await` boundary do to thread switching and work stealing on thread pools.

### task
The `coro::task<T>` is the main coroutine building block within `libcoro`.  Use task to create your coroutines and `co_await` or `co_yield` tasks within tasks to perform asynchronous operations, lazily evaluation or even spreading work out across a `coro::thread_pool`.  Tasks are lightweight and only begin execution upon awaiting them.  If their return type is not `void` then the value can be returned by const reference or by moving (r-value reference).


```C++
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Task that takes a value and doubles it.
    auto double_task = [](uint64_t x) -> coro::task<uint64_t> { co_return x* x; };

    // Create a task that awaits the doubling of its given value and
    // then returns the result after adding 5.
    auto double_and_add_5_task = [&](uint64_t input) -> coro::task<uint64_t> {
        auto doubled = co_await double_task(input);
        co_return doubled + 5;
    };

    auto output = coro::sync_wait(double_and_add_5_task(2));
    std::cout << "Task1 output = " << output << "\n";

    struct expensive_struct
    {
        std::string              id{};
        std::vector<std::string> records{};

        expensive_struct()  = default;
        ~expensive_struct() = default;

        // Explicitly delete copy constructor and copy assign, force only moves!
        // While the default move constructors will work for this struct the example
        // inserts explicit print statements to show the task is moving the value
        // out correctly.
        expensive_struct(const expensive_struct&) = delete;
        auto operator=(const expensive_struct&) -> expensive_struct& = delete;

        expensive_struct(expensive_struct&& other) : id(std::move(other.id)), records(std::move(other.records))
        {
            std::cout << "expensive_struct() move constructor called\n";
        }
        auto operator=(expensive_struct&& other) -> expensive_struct&
        {
            if (std::addressof(other) != this)
            {
                id      = std::move(other.id);
                records = std::move(other.records);
            }
            std::cout << "expensive_struct() move assignment called\n";
            return *this;
        }
    };

    // Create a very large object and return it by moving the value so the
    // contents do not have to be copied out.
    auto move_output_task = []() -> coro::task<expensive_struct> {
        expensive_struct data{};
        data.id = "12345678-1234-5678-9012-123456781234";
        for (size_t i = 10'000; i < 100'000; ++i)
        {
            data.records.emplace_back(std::to_string(i));
        }

        // Because the struct only has move contructors it will be forced to use
        // them, no need to explicitly std::move(data).
        co_return data;
    };

    auto data = coro::sync_wait(move_output_task());
    std::cout << data.id << " has " << data.records.size() << " records.\n";

    // std::unique_ptr<T> can also be used to return a larger object.
    auto unique_ptr_task = []() -> coro::task<std::unique_ptr<uint64_t>> { co_return std::make_unique<uint64_t>(42); };

    auto answer_to_everything = coro::sync_wait(unique_ptr_task());
    if (answer_to_everything != nullptr)
    {
        std::cout << "Answer to everything = " << *answer_to_everything << "\n";
    }
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto task = [](uint64_t count_to) -> coro::task<void> {
        // Create a generator function that will yield and incrementing
        // number each time its called.
        auto gen = []() -> coro::generator<uint64_t> {
            uint64_t i = 0;
            while (true)
            {
                co_yield i++;
            }
        };

        // Generate the next number until its greater than count to.
        for (auto val : gen())
        {
            std::cout << val << ", ";

            if (val >= count_to)
            {
                break;
            }
        }
        co_return;
    };

    coro::sync_wait(task(100));
}
```

Expected output:
```bash
$ ./examples/coro_generator
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### event
The `coro::event` is a thread safe async tool to have 1 or more waiters suspend for an event to be set before proceeding.  The implementation of event currently will resume execution of all waiters on the thread that sets the event.  If the event is already set when a waiter goes to wait on the thread they will simply continue executing with no suspend or wait time incurred.

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

    // Given more than a single task to synchronously wait on, use when_all() to execute all the
    // tasks concurrently on this thread and then sync_wait() for them all to complete.
    coro::sync_wait(coro::when_all(make_wait_task(e, 1), make_wait_task(e, 2), make_wait_task(e, 3), make_set_task(e)));
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

### latch
The `coro::latch` is a thread safe async tool to have 1 waiter suspend until all outstanding events have completed before proceeding.

```C++
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Complete worker tasks faster on a thread pool, using the io_scheduler version so the worker
    // tasks can yield for a specific amount of time to mimic difficult work.  The pool is only
    // setup with a single thread to showcase yield_for().
    coro::io_scheduler tp{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    // This task will wait until the given latch setters have completed.
    auto make_latch_task = [](coro::latch& l) -> coro::task<void> {
        // It seems like the dependent worker tasks could be created here, but in that case it would
        // be superior to simply do: `co_await coro::when_all(tasks);`
        // It is also important to note that the last dependent task will resume the waiting latch
        // task prior to actually completing -- thus the dependent task's frame could be destroyed
        // by the latch task completing before it gets a chance to finish after calling resume() on
        // the latch task!

        std::cout << "latch task is now waiting on all children tasks...\n";
        co_await l;
        std::cout << "latch task dependency tasks completed, resuming.\n";
        co_return;
    };

    // This task does 'work' and counts down on the latch when completed.  The final child task to
    // complete will end up resuming the latch task when the latch's count reaches zero.
    auto make_worker_task = [](coro::io_scheduler& tp, coro::latch& l, int64_t i) -> coro::task<void> {
        // Schedule the worker task onto the thread pool.
        co_await tp.schedule();
        std::cout << "worker task " << i << " is working...\n";
        // Do some expensive calculations, yield to mimic work...!  Its also important to never use
        // std::this_thread::sleep_for() within the context of coroutines, it will block the thread
        // and other tasks that are ready to execute will be blocked.
        co_await tp.yield_for(std::chrono::milliseconds{i * 20});
        std::cout << "worker task " << i << " is done, counting down on the latch\n";
        l.count_down();
        co_return;
    };

    const int64_t                 num_tasks{5};
    coro::latch                   l{num_tasks};
    std::vector<coro::task<void>> tasks{};

    // Make the latch task first so it correctly waits for all worker tasks to count down.
    tasks.emplace_back(make_latch_task(l));
    for (int64_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_worker_task(tp, l, i));
    }

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all(std::move(tasks)));
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    coro::thread_pool     tp{coro::thread_pool::options{.thread_count = 4}};
    std::vector<uint64_t> output{};
    coro::mutex           mutex;

    auto make_critical_section_task = [&](uint64_t i) -> coro::task<void> {
        co_await tp.schedule();
        // To acquire a mutex lock co_await its lock() function.  Upon acquiring the lock the
        // lock() function returns a coro::scoped_lock that holds the mutex and automatically
        // unlocks the mutex upon destruction.  This behaves just like std::scoped_lock.
        {
            auto scoped_lock = co_await mutex.lock();
            output.emplace_back(i);
        } // <-- scoped lock unlocks the mutex here.
        co_return;
    };

    const size_t                  num_tasks{100};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(num_tasks);
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_critical_section_task(i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    // The output will be variable per run depending on how the tasks are picked up on the
    // thread pool workers.
    for (const auto& value : output)
    {
        std::cout << value << ", ";
    }
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Shared mutexes require an excutor type to be able to wake up multiple shared waiters when
    // there is an exclusive lock holder releasing the lock.  This example uses a single thread
    // to also show the interleaving of coroutines acquiring the shared lock in shared and
    // exclusive mode as they resume and suspend in a linear manner.  Ideally the thread pool
    // executor would have more than 1 thread to resume all shared waiters in parallel.
    auto               tp = std::make_shared<coro::thread_pool>(coro::thread_pool::options{.thread_count = 1});
    coro::shared_mutex mutex{tp};

    auto make_shared_task = [&](uint64_t i) -> coro::task<void> {
        co_await tp->schedule();
        {
            std::cerr << "shared task " << i << " lock_shared()\n";
            auto scoped_lock = co_await mutex.lock_shared();
            std::cerr << "shared task " << i << " lock_shared() acquired\n";
            /// Immediately yield so the other shared tasks also acquire in shared state
            /// while this task currently holds the mutex in shared state.
            co_await tp->yield();
            std::cerr << "shared task " << i << " unlock_shared()\n";
        }
        co_return;
    };

    auto make_exclusive_task = [&]() -> coro::task<void> {
        co_await tp->schedule();

        std::cerr << "exclusive task lock()\n";
        auto scoped_lock = co_await mutex.lock();
        std::cerr << "exclusive task lock() acquired\n";
        // Do the exclusive work..
        std::cerr << "exclusive task unlock()\n";
        co_return;
    };

    // Create 3 shared tasks that will acquire the mutex in a shared state.
    const size_t                  num_tasks{3};
    std::vector<coro::task<void>> tasks{};
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_shared_task(i));
    }
    // Create an exclusive task.
    tasks.emplace_back(make_exclusive_task());
    // Create 3 more shared tasks that will be blocked until the exclusive task completes.
    for (size_t i = num_tasks + 1; i <= num_tasks * 2; ++i)
    {
        tasks.emplace_back(make_shared_task(i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Have more threads/tasks than the semaphore will allow for at any given point in time.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 8}};
    coro::semaphore   semaphore{2};

    auto make_rate_limited_task = [&](uint64_t task_num) -> coro::task<void>
    {
        co_await tp.schedule();

        // This will only allow 2 tasks through at any given point in time, all other tasks will
        // await the resource to be available before proceeding.
        auto result = co_await semaphore.acquire();
        if (result == coro::semaphore::acquire_result::acquired)
        {
            std::cout << task_num << ", ";
            semaphore.release();
        }
        else
        {
            std::cout << task_num << " failed to acquire semaphore [" << coro::semaphore::to_string(result) << "],";
        }
        co_return;
    };

    const size_t                  num_tasks{100};
    std::vector<coro::task<void>> tasks{};
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_rate_limited_task(i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));
}
```

Expected output, note that there is no lock around the `std::cout` so some of the output isn't perfect.
```bash
$ ./examples/coro_semaphore
1, 23, 25, 24, 22, 27, 28, 29, 21, 20, 19, 18, 17, 14, 31, 30, 33, 32, 41, 40, 37, 39, 38, 36, 35, 34, 43, 46, 47, 48, 45, 42, 44, 26, 16, 15, 13, 52, 54, 55, 53, 49, 51, 57, 58, 50, 62, 63, 61, 60, 59, 56, 12, 11, 8, 10, 9, 7, 6, 5, 4, 3, 642, , 66, 67, 6568, , 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
```

### ring_buffer
The `coro::ring_buffer<element, num_elements>` is thread safe async multi-producer multi-consumer statically sized ring buffer.  Producers that try to produce a value when the ring buffer is full will suspend until space is available.  Consumers that try to consume a value when the ring buffer is empty will suspend until space is available.  All waiters on the ring buffer for producing or consuming are resumed in a LIFO manner when their respective operation becomes available.

```C++
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    const size_t                    iterations = 100;
    const size_t                    consumers  = 4;
    coro::thread_pool               tp{coro::thread_pool::options{.thread_count = 4}};
    coro::ring_buffer<uint64_t, 16> rb{};
    coro::mutex                     m{};

    std::vector<coro::task<void>> tasks{};

    auto make_producer_task = [&]() -> coro::task<void>
    {
        co_await tp.schedule();

        for (size_t i = 1; i <= iterations; ++i)
        {
            co_await rb.produce(i);
        }

        // Wait for the ring buffer to clear all items so its a clean stop.
        while (!rb.empty())
        {
            co_await tp.yield();
        }

        // Now that the ring buffer is empty signal to all the consumers its time to stop.  Note that
        // the stop signal works on producers as well, but this example only uses 1 producer.
        {
            auto scoped_lock = co_await m.lock();
            std::cerr << "\nproducer is sending stop signal";
        }
        rb.notify_waiters();
        co_return;
    };

    auto make_consumer_task = [&](size_t id) -> coro::task<void>
    {
        co_await tp.schedule();

        while (true)
        {
            auto expected    = co_await rb.consume();
            auto scoped_lock = co_await m.lock(); // just for synchronizing std::cout/cerr
            if (!expected)
            {
                std::cerr << "\nconsumer " << id << " shutting down, stop signal received";
                break; // while
            }
            else
            {
                auto item = std::move(*expected);
                std::cout << "(id=" << id << ", v=" << item << "), ";
            }

            // Mimic doing some work on the consumed value.
            co_await tp.yield();
        }

        co_return;
    };

    // Create N consumers
    for (size_t i = 0; i < consumers; ++i)
    {
        tasks.emplace_back(make_consumer_task(i));
    }
    // Create 1 producer.
    tasks.emplace_back(make_producer_task());

    // Wait for all the values to be produced and consumed through the ring buffer.
    coro::sync_wait(coro::when_all(std::move(tasks)));
}
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

### thread_pool
`coro::thread_pool` is a statically sized pool of worker threads to execute scheduled coroutines from a FIFO queue.  To schedule a coroutine on a thread pool the pool's `schedule()` function should be `co_awaited` to transfer the execution from the current thread to a thread pool worker thread.  Its important to note that scheduling will first place the coroutine into the FIFO queue and will be picked up by the first available thread in the pool, e.g. there could be a delay if there is a lot of work queued up.

```C++
#include <coro/coro.hpp>
#include <iostream>
#include <random>

int main()
{
    coro::thread_pool tp{coro::thread_pool::options{
        // By default all thread pools will create its thread count with the
        // std::thread::hardware_concurrency() as the number of worker threads in the pool,
        // but this can be changed via this thread_count option.  This example will use 4.
        .thread_count = 4,
        // Upon starting each worker thread an optional lambda callback with the worker's
        // index can be called to make thread changes, perhaps priority or change the thread's
        // name.
        .on_thread_start_functor = [](std::size_t worker_idx) -> void {
            std::cout << "thread pool worker " << worker_idx << " is starting up.\n";
        },
        // Upon stopping each worker thread an optional lambda callback with the worker's
        // index can b called.
        .on_thread_stop_functor = [](std::size_t worker_idx) -> void {
            std::cout << "thread pool worker " << worker_idx << " is shutting down.\n";
        }}};

    auto offload_task = [&](uint64_t child_idx) -> coro::task<uint64_t> {
        // Start by scheduling this offload worker task onto the thread pool.
        co_await tp.schedule();
        // Now any code below this schedule() line will be executed on one of the thread pools
        // worker threads.

        // Mimic some expensive task that should be run on a background thread...
        std::random_device              rd;
        std::mt19937                    gen{rd()};
        std::uniform_int_distribution<> d{0, 1};

        size_t calculation{0};
        for (size_t i = 0; i < 1'000'000; ++i)
        {
            calculation += d(gen);

            // Lets be nice and yield() to let other coroutines on the thread pool have some cpu
            // time.  This isn't necessary but is illustrated to show how tasks can cooperatively
            // yield control at certain points of execution.  Its important to never call the
            // std::this_thread::sleep_for() within the context of a coroutine, that will block
            // and other coroutines which are ready for execution from starting, always use yield()
            // or within the context of a coro::io_scheduler you can use yield_for(amount).
            if (i == 500'000)
            {
                std::cout << "Task " << child_idx << " is yielding()\n";
                co_await tp.yield();
            }
        }
        co_return calculation;
    };

    auto primary_task = [&]() -> coro::task<uint64_t> {
        const size_t                      num_children{10};
        std::vector<coro::task<uint64_t>> child_tasks{};
        child_tasks.reserve(num_children);
        for (size_t i = 0; i < num_children; ++i)
        {
            child_tasks.emplace_back(offload_task(i));
        }

        // Wait for the thread pool workers to process all child tasks.
        auto results = co_await coro::when_all(std::move(child_tasks));

        // Sum up the results of the completed child tasks.
        size_t calculation{0};
        for (const auto& task : results)
        {
            calculation += task.return_value();
        }
        co_return calculation;
    };

    auto result = coro::sync_wait(primary_task());
    std::cout << "calculated thread pool result = " << result << "\n";
}
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

### io_scheduler
`coro::io_scheduler` is a i/o event scheduler that can use two methods of task processing:

* A background `coro::thread_pool`
* Inline task processing on the `coro::io_scheduler`'s event loop

Using a background `coro::thread_pool` will default to using `(std::thread::hardware_concurrency() - 1)` threads to process tasks.  This processing strategy is best for longer tasks that would block the i/o scheduler or for tasks that are latency sensitive.

Using the inline processing strategy will have the event loop i/o thread process the tasks inline on that thread when events are received.  This processing strategy is best for shorter task that will not block the i/o thread for long or for pure throughput by using thread per core architecture, e.g. spin up an inline i/o scheduler per core and inline process tasks on each scheduler.

The `coro::io_scheduler` can use a dedicated spawned thread for processing events that are ready or it can be maually driven via its `process_events()` function for integration into existing event loops.  By default i/o schedulers will spawn a dedicated event thread and use a thread pool to process tasks.

Before getting to an example there are two methods of scheduling work onto an i/o scheduler, the first is by having the caller maintain the lifetime of the task being scheduled and the second is by moving or transfering owership of the task into the i/o scheduler.  The first can allow for return values but requires the caller to manage the lifetime of the coroutine while the second requires the return type of the task to be void but allows for variable or unknown task lifetimes.  Transferring task lifetime to the scheduler can be useful, e.g. for a network request.

The example provided here shows an i/o scheduler that spins up a basic `coro::net::tcp_server` and a `coro::net::tcp_client` that will connect to each other and then send a request and a response.

```C++
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = std::make_shared<coro::io_scheduler>(coro::io_scheduler::options{
        // The scheduler will spawn a dedicated event processing thread.  This is the default, but
        // it is possible to use 'manual' and call 'process_events()' to drive the scheduler yourself.
        .thread_strategy = coro::io_scheduler::thread_strategy_t::spawn,
        // If the scheduler is in spawn mode this functor is called upon starting the dedicated
        // event processor thread.
        .on_io_thread_start_functor = [] { std::cout << "io_scheduler::process event thread start\n"; },
        // If the scheduler is in spawn mode this functor is called upon stopping the dedicated
        // event process thread.
        .on_io_thread_stop_functor = [] { std::cout << "io_scheduler::process event thread stop\n"; },
        // The io scheduler can use a coro::thread_pool to process the events or tasks it is given.
        // You can use an execution strategy of `process_tasks_inline` to have the event loop thread
        // directly process the tasks, this might be desirable for small tasks vs a thread pool for large tasks.
        .pool =
            coro::thread_pool::options{
                .thread_count            = 2,
                .on_thread_start_functor = [](size_t i)
                { std::cout << "io_scheduler::thread_pool worker " << i << " starting\n"; },
                .on_thread_stop_functor = [](size_t i)
                { std::cout << "io_scheduler::thread_pool worker " << i << " stopping\n"; },
            },
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});

    auto make_server_task = [&]() -> coro::task<void>
    {
        // Start by creating a tcp server, we'll do this before putting it into the scheduler so
        // it is immediately available for the client to connect since this will create a socket,
        // bind the socket and start listening on that socket.  See tcp_server for more details on
        // how to specify the local address and port to bind to as well as enabling SSL/TLS.
        coro::net::tcp_server server{scheduler};

        // Now scheduler this task onto the scheduler.
        co_await scheduler->schedule();

        // Wait for an incoming connection and accept it.
        auto poll_status = co_await server.poll();
        if (poll_status != coro::poll_status::event)
        {
            co_return; // Handle error, see poll_status for detailed error states.
        }

        // Accept the incoming client connection.
        auto client = server.accept();

        // Verify the incoming connection was accepted correctly.
        if (!client.socket().is_valid())
        {
            co_return; // Handle error.
        }

        // Now wait for the client message, this message is small enough it should always arrive
        // with a single recv() call.
        poll_status = co_await client.poll(coro::poll_op::read);
        if (poll_status != coro::poll_status::event)
        {
            co_return; // Handle error.
        }

        // Prepare a buffer and recv() the client's message.  This function returns the recv() status
        // as well as a span<char> that overlaps the given buffer for the bytes that were read.  This
        // can be used to resize the buffer or work with the bytes without modifying the buffer at all.
        std::string request(256, '\0');
        auto [recv_status, recv_bytes] = client.recv(request);
        if (recv_status != coro::net::recv_status::ok)
        {
            co_return; // Handle error, see net::recv_status for detailed error states.
        }

        request.resize(recv_bytes.size());
        std::cout << "server: " << request << "\n";

        // Make sure the client socket can be written to.
        poll_status = co_await client.poll(coro::poll_op::write);
        if (poll_status != coro::poll_status::event)
        {
            co_return; // Handle error.
        }

        // Send the server response to the client.
        // This message is small enough that it will be sent in a single send() call, but to demonstrate
        // how to use the 'remaining' portion of the send() result this is wrapped in a loop until
        // all the bytes are sent.
        std::string           response  = "Hello from server.";
        std::span<const char> remaining = response;
        do
        {
            // Optimistically send() prior to polling.
            auto [send_status, r] = client.send(remaining);
            if (send_status != coro::net::send_status::ok)
            {
                co_return; // Handle error, see net::send_status for detailed error states.
            }

            if (r.empty())
            {
                break; // The entire message has been sent.
            }

            // Re-assign remaining bytes for the next loop iteration and poll for the socket to be
            // able to be written to again.
            remaining    = r;
            auto pstatus = co_await client.poll(coro::poll_op::write);
            if (pstatus != coro::poll_status::event)
            {
                co_return; // Handle error.
            }
        } while (true);

        co_return;
    };

    auto make_client_task = [&]() -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        // Create the tcp_client with the default settings, see tcp_client for how to set the
        // ip address, port, and optionally enabling SSL/TLS.
        coro::net::tcp_client client{scheduler};

        // Ommitting error checking code for the client, each step should check the status and
        // verify the number of bytes sent or received.

        // Connect to the server.
        co_await client.connect();

        // Make sure the client socket can be written to.
        co_await client.poll(coro::poll_op::write);

        // Send the request data.
        client.send(std::string_view{"Hello from client."});

        // Wait for the response and receive it.
        co_await client.poll(coro::poll_op::read);
        std::string response(256, '\0');
        auto [recv_status, recv_bytes] = client.recv(response);
        response.resize(recv_bytes.size());

        std::cout << "client: " << response << "\n";
        co_return;
    };

    // Create and wait for the server and client tasks to complete.
    coro::sync_wait(coro::when_all(make_server_task(), make_client_task()));
}
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

### task_container
`coro::task_container` is a special container type that will maintain the lifetime of tasks that do not have a known lifetime.  This is extremely useful for tasks that hold open connections to clients and possibly process multiple requests from that client before shutting down.  The task doesn't know how long it will be alive but at some point in the future it will complete and need to have its resources cleaned up.  The `coro::task_container` does this by wrapping the users task into anothe coroutine task that will mark itself for deletion upon completing within the parent task container.  The task container should then run garbage collection periodically, or by default when a new task is added, to prune completed tasks from the container.

All tasks that are stored within a `coro::task_container` must have a `void` return type since their result cannot be accessed due to the task's lifetime being indeterminate.

```C++
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_server_task = [&]() -> coro::task<void> {
        // This is the task that will handle processing a client's requests.
        auto serve_client = [](coro::net::tcp_client client) -> coro::task<void> {
            size_t requests{1};

            while (true)
            {
                // Continue to accept more requests until the client closes the connection.
                co_await client.poll(coro::poll_op::read);

                std::string request(64, '\0');
                auto [recv_status, recv_bytes] = client.recv(request);
                if (recv_status == coro::net::recv_status::closed)
                {
                    break;
                }

                request.resize(recv_bytes.size());
                std::cout << "server: " << request << "\n";

                // Make sure the client socket can be written to.
                co_await client.poll(coro::poll_op::write);

                auto response = "Hello from server " + std::to_string(requests);
                client.send(response);

                ++requests;
            }

            co_return;
        };

        // Spin up the tcp_server and schedule it onto the io_scheduler.
        coro::net::tcp_server server{scheduler};
        co_await scheduler->schedule();

        // All incoming connections will be stored into the task container until they are completed.
        coro::task_container tc{scheduler};

        // Wait for an incoming connection and accept it, this example will only use 1 connection.
        co_await server.poll();
        auto client = server.accept();
        // Store the task that will serve the client into the container and immediately begin executing it
        // on the task container's thread pool, which is the same as the scheduler.
        tc.start(serve_client(std::move(client)));

        // Wait for all clients to complete before shutting down the tcp_server.
        co_await tc.garbage_collect_and_yield_until_empty();
        co_return;
    };

    auto make_client_task = [&](size_t request_count) -> coro::task<void> {
        co_await scheduler->schedule();
        coro::net::tcp_client client{scheduler};

        co_await client.connect();

        // Send N requests on the same connection and wait for the server response to each one.
        for (size_t i = 1; i <= request_count; ++i)
        {
            // Make sure the client socket can be written to.
            co_await client.poll(coro::poll_op::write);

            // Send the request data.
            auto request = "Hello from client " + std::to_string(i);
            client.send(request);

            co_await client.poll(coro::poll_op::read);
            std::string response(64, '\0');
            auto [recv_status, recv_bytes] = client.recv(response);
            response.resize(recv_bytes.size());

            std::cout << "client: " << response << "\n";
        }

        co_return; // Upon exiting the tcp_client will close its connection to the server.
    };

    coro::sync_wait(coro::when_all(make_server_task(), make_client_task(5)));
}
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

### Requirements
    C++20 Compiler with coroutine support
        g++10.2.1
        g++10.3.1
        g++11
        g++12
        g++13
    CMake
    make or ninja
    pthreads
    openssl
    gcov/lcov (For generating coverage only)

### Instructions

#### Tested Distos

 * ubuntu:20.04, 22.04
 * fedora:32-37
 * openSUSE/leap:15.2

#### Cloning the project
This project uses git submodules, to properly checkout this project use:

    git clone --recurse-submodules <libcoro-url>

This project depends on the following git sub-modules:
 * [libc-ares](https://github.com/c-ares/c-ares) For async DNS resolver, this is a git submodule.
 * [catch2](https://github.com/catchorg/Catch2) For testing, this is embedded in the `test/` directory.
 * [expected](https://github.com/TartanLlama/expected) For results on operations that can fail, this is a git submodule in the `vendor/` directory.

#### Building
    mkdir Release && cd Release
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build .

CMake Options:

| Name                          | Default | Description                                                                   |
|:------------------------------|:--------|:------------------------------------------------------------------------------|
| LIBCORO_EXTERNAL_DEPENDENCIES | OFF     | Use CMake find_package to resolve dependencies instead of embedded libraries. |
| LIBCORO_BUILD_TESTS           | ON      | Should the tests be built?                                                    |
| LIBCORO_CODE_COVERAGE         | OFF     | Should code coverage be enabled? Requires tests to be enabled.                |
| LIBCORO_BUILD_EXAMPLES        | ON      | Should the examples be built?                                                 |
| LIBCORO_FEATURE_THREADING     | ON      | Include the features depending on multi-threading support by the standard lib.                                                  |
| LIBCORO_FEATURE_NETWORKING    | ON      | Include networking features.                                                  |
| LIBCORO_FEATURE_SSL           | ON      | Include SSL features. Requires networking to be enabled.                                                  |

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

#### Tests
The tests will automatically be run by github actions on creating a pull request.  They can also be ran locally:

    # Invoke via cmake with all output from the tests displayed to console:
    ctest -VV

    # Or invoke directly, can pass the name of tests to execute, the framework used is catch2.
    # Tests are tagged with their group, below is howt o run all of the coro::net::tcp_server tests:
    ./Debug/test/libcoro_test "[tcp_server]"

### Support

File bug reports, feature requests and questions using [GitHub libcoro Issues](https://github.com/jbaldwin/libcoro/issues)

Copyright © 2020-2022 Josh Baldwin

[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg
[badge.license]: https://img.shields.io/badge/license-Apache--2.0-blue

[language]: https://en.wikipedia.org/wiki/C%2B%2B17
[license]: https://en.wikipedia.org/wiki/Apache_License
