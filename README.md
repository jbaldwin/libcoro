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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // This lambda will create a coro::task that returns a unit64_t.
    // It can be invoked many times with different arguments.
    auto make_task_inline = [](uint64_t x) -> coro::task<uint64_t> { co_return x + x; };

    // This will block the calling thread until the created task completes.
    // Since this task isn't scheduled on any coro::thread_pool or coro::scheduler
    // it will execute directly on the calling thread.
    auto result = coro::sync_wait(make_task_inline(5));
    std::cout << "Inline Result = " << result << "\n";

    // We'll make a 1 thread coro::thread_pool to demonstrate offloading the task's
    // execution to another thread.  We'll pass the thread pool as a parameter so
    // the task can be scheduled.
    // Note that you will need to guarantee the thread pool outlives the coroutine.
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});

    auto make_task_offload = [](std::unique_ptr<coro::thread_pool>& tp, uint64_t x) -> coro::task<uint64_t>
    {
        co_await tp->schedule(); // Schedules execution on the thread pool.
        co_return x + x;         // This will execute on the thread pool.
    };

    // This will still block the calling thread, but it will now offload to the
    // coro::thread_pool since the coroutine task is immediately scheduled.
    result = coro::sync_wait(make_task_offload(tp, 10));
    std::cout << "Offload Result = " << result << "\n";
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Create a thread pool to execute all the tasks in parallel.
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 4});
    // Create the task we want to invoke multiple times and execute in parallel on the thread pool.
    auto twice = [](std::unique_ptr<coro::thread_pool>& tp, uint64_t x) -> coro::task<uint64_t>
    {
        co_await tp->schedule(); // Schedule onto the thread pool.
        co_return x + x;        // Executed on the thread pool.
    };

    // Make our tasks to execute, tasks can be passed in via a std::ranges::range type or var args.
    std::vector<coro::task<uint64_t>> tasks{};
    for (std::size_t i = 0; i < 5; ++i)
    {
        tasks.emplace_back(twice(tp, i + 1));
    }

    // Synchronously wait on this thread for the thread pool to finish executing all the tasks in parallel.
    auto results = coro::sync_wait(coro::when_all(std::move(tasks)));
    for (auto& result : results)
    {
        // If your task can throw calling return_value() will either return the result or re-throw the exception.
        try
        {
            std::cout << result.return_value() << "\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << '\n';
        }
    }

    // Use var args instead of a container as input to coro::when_all.
    auto square = [](std::unique_ptr<coro::thread_pool>& tp, double x) -> coro::task<double>
    {
        co_await tp->schedule();
        co_return x* x;
    };

    // Var args allows you to pass in tasks with different return types and returns
    // the result as a std::tuple.
    auto tuple_results = coro::sync_wait(coro::when_all(square(tp, 1.1), twice(tp, 10)));

    auto first  = std::get<0>(tuple_results).return_value();
    auto second = std::get<1>(tuple_results).return_value();

    std::cout << "first: " << first << " second: " << second << "\n";
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Create a scheduler to execute all tasks in parallel and also so we can
    // suspend a task to act like a timeout event.
    auto scheduler = coro::scheduler::make_unique();

    // This task will behave like a long running task and will produce a valid result.
    auto make_long_running_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                                     std::chrono::milliseconds           execution_time) -> coro::task<int64_t>
    {
        // Schedule the task to execute in parallel.
        co_await scheduler->schedule();
        // Fake doing some work...
        co_await scheduler->yield_for(execution_time);
        // Return the result.
        co_return 1;
    };

    auto make_timeout_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<int64_t>
    {
        // Schedule a timer to be fired so we know the task timed out.
        co_await scheduler->schedule_after(std::chrono::milliseconds{100});
        co_return -1;
    };

    // Example showing the long running task completing first.
    {
        std::vector<coro::task<int64_t>> tasks{};
        tasks.emplace_back(make_long_running_task(scheduler, std::chrono::milliseconds{50}));
        tasks.emplace_back(make_timeout_task(scheduler));

        auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
        std::cout << "result = " << result << "\n";
    }

    // Example showing the long running task timing out.
    {
        std::vector<coro::task<int64_t>> tasks{};
        tasks.emplace_back(make_long_running_task(scheduler, std::chrono::milliseconds{500}));
        tasks.emplace_back(make_timeout_task(scheduler));

        auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
        std::cout << "result = " << result << "\n";
    }
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Create a task that awaits the doubling of its given value and
    // then returns the result after adding 5.
    auto double_and_add_5_task = [](uint64_t input) -> coro::task<uint64_t>
    {
        // Task that takes a value and doubles it.
        auto double_task = [](uint64_t x) -> coro::task<uint64_t> { co_return x * 2; };

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
        expensive_struct(const expensive_struct&)                    = delete;
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
    auto move_output_task = []() -> coro::task<expensive_struct>
    {
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
    auto task = [](uint64_t count_to) -> coro::task<void>
    {
        // Create a generator function that will yield and incrementing
        // number each time its called.
        auto gen = []() -> coro::generator<uint64_t>
        {
            uint64_t i = 0;
            while (true)
            {
                co_yield i;
                ++i;
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
    auto make_wait_task = [](const coro::event& e, uint64_t i) -> coro::task<void>
    {
        std::cout << "task " << i << " is waiting on the event...\n";
        co_await e;
        std::cout << "task " << i << " event triggered, now resuming.\n";
        co_return;
    };

    // This task will trigger the event allowing all waiting tasks to proceed.
    auto make_set_task = [](coro::event& e) -> coro::task<void>
    {
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
    // Complete worker tasks faster on a thread pool, using the scheduler version so the worker
    // tasks can yield for a specific amount of time to mimic difficult work.  The pool is only
    // setup with a single thread to showcase yield_for().
    auto tp = coro::scheduler::make_unique(
        coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    // This task will wait until the given latch setters have completed.
    auto make_latch_task = [](coro::latch& l) -> coro::task<void>
    {
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
    auto make_worker_task = [](std::unique_ptr<coro::scheduler>& tp, coro::latch& l, int64_t i) -> coro::task<void>
    {
        // Schedule the worker task onto the thread pool.
        co_await tp->schedule();
        std::cout << "worker task " << i << " is working...\n";
        // Do some expensive calculations, yield to mimic work...!  Its also important to never use
        // std::this_thread::sleep_for() within the context of coroutines, it will block the thread
        // and other tasks that are ready to execute will be blocked.
        co_await tp->yield_for(std::chrono::milliseconds{i * 20});
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
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 4});
    std::vector<uint64_t> output{};
    coro::mutex           mutex{};

    auto make_critical_section_task =
        [](std::unique_ptr<coro::thread_pool>& tp, coro::mutex& mutex, std::vector<uint64_t>& output, uint64_t i) -> coro::task<void>
    {
        co_await tp->schedule();
        // To acquire a mutex lock co_await its scoped_lock() function. Upon acquiring the lock the
        // scoped_lock() function returns a coro::scoped_lock that holds the mutex and automatically
        // unlocks the mutex upon destruction. This behaves just like std::scoped_lock.
        {
            auto scoped_lock = co_await mutex.scoped_lock();
            output.emplace_back(i);
        } // <-- scoped lock unlocks the mutex here.
        co_return;
    };

    const size_t                  num_tasks{100};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(num_tasks);
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_critical_section_task(tp, mutex, output, i));
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
    // Shared mutexes require an executor type to be able to wake up multiple shared waiters when
    // there is an exclusive lock holder releasing the lock. This example uses a single thread
    // to also show the interleaving of coroutines acquiring the shared lock in shared and
    // exclusive mode as they resume and suspend in a linear manner. Ideally the thread pool
    // executor would have more than 1 thread to resume all shared waiters in parallel.
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::shared_mutex<coro::thread_pool> mutex{tp};

    auto make_shared_task = [](std::unique_ptr<coro::thread_pool>&     tp,
                               coro::shared_mutex<coro::thread_pool>& mutex,
                               uint64_t                               i) -> coro::task<void>
    {
        co_await tp->schedule();
        std::cerr << "shared task " << i << " lock_shared()\n";

        // Note that to have a scoped shared lock a task must be passed in for the shared scoped lock to callback,
        // this is required since coro::shared_mutex.unlock() and unlock_shared() are coroutines and cannot be
        // awaited in a RAII destructor like coro::mutex.
        co_await mutex.scoped_lock_shared([](std::unique_ptr<coro::thread_pool>& tp, uint64_t i) -> coro::task<void>
        {
            std::cerr << "shared task " << i << " lock_shared() acquired\n";
            /// Immediately yield so the other shared tasks also acquire in shared state
            /// while this task currently holds the mutex in shared state.
            co_await tp->yield();
            std::cerr << "shared task " << i << " unlock_shared()\n";
        }(tp, i));
        co_return;
    };

    auto make_exclusive_task = [](std::unique_ptr<coro::thread_pool>&     tp,
                                  coro::shared_mutex<coro::thread_pool>& mutex) -> coro::task<void>
    {
        co_await tp->schedule();
        co_await mutex.scoped_lock([](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<void>
        {
            std::cerr << "exclusive task lock()\n";
            std::cerr << "exclusive task lock() acquired\n";
            // Do the exclusive work...
            co_await tp->yield();
            std::cerr << "exclusive task unlock()\n";
        }(tp));
        co_return;
    };

    // Create 3 shared tasks that will acquire the mutex in a shared state.
    constexpr size_t              num_tasks{3};
    std::vector<coro::task<void>> tasks{};
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_shared_task(tp, mutex, i));
    }
    // Create an exclusive task.
    tasks.emplace_back(make_exclusive_task(tp, mutex));
    // Create 3 more shared tasks that will be blocked until the exclusive task completes.
    for (size_t i = num_tasks + 1; i <= num_tasks * 2; ++i)
    {
        tasks.emplace_back(make_shared_task(tp, mutex, i));
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
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 8});
    coro::semaphore<2>   semaphore{2};

    auto make_rate_limited_task =
        [](std::unique_ptr<coro::thread_pool>& tp, coro::semaphore<2>& semaphore, uint64_t task_num) -> coro::task<void>
    {
        co_await tp->schedule();

        // This will only allow 2 tasks through at any given point in time, all other tasks will
        // await the resource to be available before proceeding.
        auto result = co_await semaphore.acquire();
        if (result == coro::semaphore_acquire_result::acquired)
        {
            std::cout << task_num << ", ";
            co_await semaphore.release();
        }
        else
        {
            std::cout << task_num << " failed to acquire semaphore [" << coro::to_string(result) << "],";
        }
        co_return;
    };

    const size_t                  num_tasks{100};
    std::vector<coro::task<void>> tasks{};
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_rate_limited_task(tp, semaphore, i));
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
    auto                            tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 4});
    coro::ring_buffer<uint64_t, 16> rb{};
    coro::mutex                     m{};

    std::vector<coro::task<void>> tasks{};

    auto make_producer_task =
        [](std::unique_ptr<coro::thread_pool>& tp, coro::ring_buffer<uint64_t, 16>& rb, coro::mutex& m) -> coro::task<void>
    {
        co_await tp->schedule();

        for (size_t i = 1; i <= iterations; ++i)
        {
            co_await rb.produce(i);
        }

        // Now that the ring buffer is empty signal to all the consumers its time to stop.  Note that
        // the stop signal works on producers as well, but this example only uses 1 producer.
        {
            auto scoped_lock = co_await m.scoped_lock();
            std::cerr << "\nproducer is sending shutdown signal with drain";
        }
        co_await rb.shutdown_drain(tp);
        co_return;
    };

    auto make_consumer_task =
        [](std::unique_ptr<coro::thread_pool>& tp, coro::ring_buffer<uint64_t, 16>& rb, coro::mutex& m, size_t id) -> coro::task<void>
    {
        co_await tp->schedule();

        while (true)
        {
            auto expected    = co_await rb.consume();
            auto scoped_lock = co_await m.scoped_lock(); // just for synchronizing std::cout/cerr
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
            co_await tp->yield();
        }

        co_return;
    };

    // Create N consumers
    for (size_t i = 0; i < consumers; ++i)
    {
        tasks.emplace_back(make_consumer_task(tp, rb, m, i));
    }
    // Create 1 producer.
    tasks.emplace_back(make_producer_task(tp, rb, m));

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

### queue
The `coro::queue<element_type>` is thread safe async multi-producer multi-consumer queue. Producing into the queue is not an asynchronous operation, it will either immediately use a consumer that is awaiting on `pop()` to process the element, or if no consumer is available place the element into the queue. All consume waiters on the queue are resumed in a LIFO manner when an element becomes available to consume.

```C++
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    const size_t iterations      = 5;
    const size_t producers_count = 5;
    const size_t consumers_count = 2;

    auto tp = coro::thread_pool::make_unique();
    coro::queue<uint64_t> q{};
    coro::latch           producers_done{producers_count};
    coro::mutex           m{}; /// Just for making the console prints look nice.

    auto make_producer_task =
        [](std::unique_ptr<coro::thread_pool>& tp, coro::queue<uint64_t>& q, coro::latch& pd) -> coro::task<void>
    {
        co_await tp->schedule();

        for (size_t i = 0; i < iterations; ++i)
        {
            co_await q.push(i);
        }

        pd.count_down(); // Notify the shutdown task this producer is complete.
        co_return;
    };

    auto make_shutdown_task = [](std::unique_ptr<coro::thread_pool>& tp, coro::queue<uint64_t>& q, coro::latch& pd) -> coro::task<void>
    {
        // This task will wait for all the producers to complete and then for the
        // entire queue to be drained before shutting it down.
        co_await tp->schedule();
        co_await pd;
        co_await q.shutdown_drain(tp);
        co_return;
    };

    auto make_consumer_task = [](std::unique_ptr<coro::thread_pool>& tp, coro::queue<uint64_t>& q, coro::mutex& m) -> coro::task<void>
    {
        co_await tp->schedule();

        while (true)
        {
            auto expected = co_await q.pop();
            if (!expected)
            {
                break; // coro::queue is shutting down
            }

            auto scoped_lock = co_await m.scoped_lock(); // Only used to make the output look nice.
            std::cout << "consumed " << *expected << "\n";
        }
    };

    std::vector<coro::task<void>> tasks{};

    for (size_t i = 0; i < producers_count; ++i)
    {
        tasks.push_back(make_producer_task(tp, q, producers_done));
    }
    for (size_t i = 0; i < consumers_count; ++i)
    {
        tasks.push_back(make_consumer_task(tp, q, m));
    }
    tasks.push_back(make_shutdown_task(tp, q, producers_done));

    coro::sync_wait(coro::when_all(std::move(tasks)));
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = coro::scheduler::make_unique();
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<uint64_t> condition{0};
    std::stop_source ss{};

    auto make_waiter_task = [](std::unique_ptr<coro::scheduler>& scheduler, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss, std::atomic<uint64_t>& condition, int64_t id) -> coro::task<void>
    {
        co_await scheduler->schedule();
        while (true)
        {
            // Consume the condition until a stop is requested.
            auto lock = co_await m.scoped_lock();
            auto ready = co_await cv.wait(lock, ss.get_token(), [&id, &condition]() -> bool
                {
                    std::cerr << id << " predicate condition = " << condition << "\n";
                    return condition > 0;
                });
            std::cerr << id << " waiter condition = " << condition << "\n";

            if (ready)
            {
                // Handle event.

                // It is worth noting that because condition variables must hold the lock to wake up they are naturally serialized.
                // It is wise once the condition data is acquired the lock should be released and then spawn off any work into another task via the scheduler.
                condition--;
                lock.unlock();
                // We'll yield for a bit to mimic work.
                co_await scheduler->yield_for(std::chrono::milliseconds{10});
            }

            // Was this wake-up due to being stopped?
            if (ss.stop_requested())
            {
                std::cerr << id << " ss.stop_requsted() co_return\n";
                co_return;
            }
        }
    };

    auto make_notifier_task = [](std::unique_ptr<coro::scheduler>& scheduler, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss, std::atomic<uint64_t>& condition) -> coro::task<void>
    {
        // To make this example more deterministic the notifier will wait between each notify event to showcase
        // how exactly the condition variable will behave with the condition in certain states and the notify_one or notify_all.
        co_await scheduler->schedule_after(std::chrono::milliseconds{50});

        std::cerr << "cv.notify_one() condition = 0\n";
        co_await cv.notify_one(); // Predicate will fail condition == 0.
        {
            // To guarantee the condition is 'updated' in the predicate it must be done behind the lock.
            auto lock = co_await m.scoped_lock();
            condition++;
        }
        // Notifying does not need to hold the lock.
        std::cerr << "cv.notify_one() condition = 1\n";
        co_await cv.notify_one(); // Predicate will pass condition == 1.

        co_await scheduler->schedule_after(std::chrono::milliseconds{50});
        {
            auto lock = co_await m.scoped_lock();
            condition += 2;
        }
        std::cerr << "cv.notify_all() condition = 2\n";
        co_await cv.notify_all(); // Predicates will pass condition == 2 then condition == 1.

        co_await scheduler->schedule_after(std::chrono::milliseconds{50});
        {
            auto lock = co_await m.scoped_lock();
            condition++;
        }
        std::cerr << "cv.notify_all() condition = 1\n";
        co_await cv.notify_all(); // Predicates will pass condition == 1 then Predicate will not pass condition == 0.

        co_await scheduler->schedule_after(std::chrono::milliseconds{50});
        {
            auto lock = co_await m.scoped_lock();
        }
        std::cerr << "ss.request_stop()\n";
        // To stop set the stop source to stop and then notify all waiters.
        ss.request_stop();
        co_await cv.notify_all();
        co_return;
    };

    coro::sync_wait(
        coro::when_all(
            make_waiter_task(scheduler, cv, m, ss, condition, 0),
            make_waiter_task(scheduler, cv, m, ss, condition, 1),
            make_notifier_task(scheduler, cv, m, ss, condition)));

    return 0;
}
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto tp = coro::thread_pool::make_unique();

    int a = 1;
    int b = 2;
    int c = 3;

    auto make_task_with_captures = [&tp, &c](int d, int e) -> coro::task<int>
    {
        // Mimic a suspension so lambda captures would normally be dangling/destroyed.
        co_await tp->yield();
        co_return c + d + e;
    };

    // This is bad form, the captures will be dangling after `co_await tp->yield();`.
    // coro::sync_wait(make_task_with_captures(a, b));

    // This is good form, the coro::invoke will create a stable coroutine frame for the lambda captures.
    std::cout << coro::sync_wait(coro::invoke(make_task_with_captures, a, b));
}
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
#include <coro/coro.hpp>
#include <iostream>
#include <random>

int main()
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{
        // By default all thread pools will create its thread count with the
        // std::thread::hardware_concurrency() as the number of worker threads in the pool,
        // but this can be changed via this thread_count option.  This example will use 4.
        .thread_count = 4,
        // Upon starting each worker thread an optional lambda callback with the worker's
        // index can be called to make thread changes, perhaps priority or change the thread's
        // name.
        .on_thread_start_functor = [](std::size_t worker_idx) -> void
        { std::cout << "thread pool worker " << worker_idx << " is starting up.\n"; },
        // Upon stopping each worker thread an optional lambda callback with the worker's
        // index can b called.
        .on_thread_stop_functor = [](std::size_t worker_idx) -> void
        { std::cout << "thread pool worker " << worker_idx << " is shutting down.\n"; }});

    auto primary_task = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
    {
        auto offload_task = [](std::unique_ptr<coro::thread_pool>& tp, uint64_t child_idx) -> coro::task<uint64_t>
        {
            // Start by scheduling this offload worker task onto the thread pool.
            co_await tp->schedule();
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
                // or within the context of a coro::scheduler you can use yield_for(amount).
                if (i == 500'000)
                {
                    std::cout << "Task " << child_idx << " is yielding()\n";
                    co_await tp->yield();
                }
            }
            co_return calculation;
        };

        const size_t                      num_children{10};
        std::vector<coro::task<uint64_t>> child_tasks{};
        child_tasks.reserve(num_children);
        for (size_t i = 0; i < num_children; ++i)
        {
            child_tasks.emplace_back(offload_task(tp, i));
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

    auto result = coro::sync_wait(primary_task(tp));
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
#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = coro::scheduler::make_unique(
        coro::scheduler::options{
            // The scheduler will spawn a dedicated event processing thread.  This is the default, but
            // it is possible to use 'manual' and call 'process_events()' to drive the scheduler yourself.
            .thread_strategy = coro::scheduler::thread_strategy_t::spawn,
            // If the scheduler is in spawn mode this functor is called upon starting the dedicated
            // event processor thread.
            .on_io_thread_start_functor = [] { std::cout << "scheduler::process event thread start\n"; },
            // If the scheduler is in spawn mode this functor is called upon stopping the dedicated
            // event process thread.
            .on_io_thread_stop_functor = [] { std::cout << "scheduler::process event thread stop\n"; },
            // The io scheduler can use a coro::thread_pool to process the events or tasks it is given.
            // You can use an execution strategy of `process_tasks_inline` to have the event loop thread
            // directly process the tasks, this might be desirable for small tasks vs a thread pool for large tasks.
            .pool =
                coro::thread_pool::options{
                    .thread_count            = 2,
                    .on_thread_start_functor = [](size_t i)
                    { std::cout << "scheduler::thread_pool worker " << i << " starting\n"; },
                    .on_thread_stop_functor = [](size_t i)
                    { std::cout << "scheduler::thread_pool worker " << i << " stopping\n"; },
                },
            .execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_on_thread_pool});

    auto make_server_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        // Start by creating a tcp server, we'll do this before putting it into the scheduler so
        // it is immediately available for the client to connect since this will create a socket,
        // bind the socket and start listening on that socket.
        coro::net::tcp::server server{scheduler, {"127.0.0.1", 8080}};

        // Now schedule this task onto the scheduler.
        co_await scheduler->schedule();

        // Wait for an incoming connection and accept it.
        auto client = co_await server.accept();

        // Verify the incoming connection was accepted correctly.
        if (!client)
        {
            std::cout << "server error: " << client.error().message() << "\n";
            co_return; // Handle error.
        }

        // Prepare a buffer and read_some() the client's message.  This function returns the read_some() status
        // as well as a span<std::byte> that overlaps the given buffer for the bytes that were read. This
        // can be used to resize the buffer or work with the bytes without modifying the buffer at all.
        std::string request(256, '\0');
        auto [read_status, read_bytes] = co_await client->read_some(request);
        if (!read_status.is_ok())
        {
            std::cout << "server error: " << read_status.message() << "\n";
            co_return; // Handle error, see net::io_status for detailed error stats.
        }

        request.resize(read_bytes.size());
        std::cout << "server: " << request << "\n";

        // Send the server response to the client.
        std::string response        = "Hello from server.";
        auto [write_status, unsent] = co_await client->write_all(response);
        if (!write_status.is_ok())
        {
            std::cout << "server error: " << write_status.message() << "\n";
            co_return; // Handle error, see net::io_status for detailed error stats.
        }

        co_return;
    };

    auto make_client_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        // Create the tcp::client
        coro::net::tcp::client client{scheduler, {"127.0.0.1", 8080}};

        // Ommitting error checking code for the client, each step should check the status and
        // verify the number of bytes sent or received.

        // Connect to the server.
        co_await client.connect();

        // Send the request data.
        co_await client.write_all(std::string_view{"Hello from client."});

        // Wait for the response and receive it.
        std::string response(256, '\0');
        auto [read_status, read_bytes] = co_await client.read_some(response);
        response.resize(read_bytes.size());

        std::cout << "client: " << response << "\n";
        co_return;
    };

    // Create and wait for the server and client tasks to complete.
    coro::sync_wait(coro::when_all(make_server_task(scheduler), make_client_task(scheduler)));
}
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
