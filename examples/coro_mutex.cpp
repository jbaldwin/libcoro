#include <coro/coro.hpp>
#include <iostream>

int main()
{
    coro::thread_pool     tp{coro::thread_pool::options{.thread_count = 4}};
    std::vector<uint64_t> output{};
    coro::mutex           mutex;

    auto make_critical_section_task =
        [](coro::thread_pool& tp, coro::mutex& mutex, std::vector<uint64_t>& output, uint64_t i) -> coro::task<void>
    {
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
