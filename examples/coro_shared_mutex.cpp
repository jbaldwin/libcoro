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
