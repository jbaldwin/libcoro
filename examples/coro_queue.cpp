#include <coro/coro.hpp>
#include <iostream>

int main()
{
    const size_t iterations      = 5;
    const size_t producers_count = 5;
    const size_t consumers_count = 2;

    coro::thread_pool     tp{};
    coro::queue<uint64_t> q{};
    coro::latch           producers_done{producers_count};
    coro::mutex           m{}; /// Just for making the console prints look nice.

    auto make_producer_task =
        [iterations](coro::thread_pool& tp, coro::queue<uint64_t>& q, coro::latch& pd) -> coro::task<void>
    {
        co_await tp.schedule();

        for (size_t i = 0; i < iterations; ++i)
        {
            co_await q.push(i);
        }

        pd.count_down(); // Notify the shutdown task this producer is complete.
        co_return;
    };

    auto make_shutdown_task = [](coro::thread_pool& tp, coro::queue<uint64_t>& q, coro::latch& pd) -> coro::task<void>
    {
        // This task will wait for all the producers to complete and then for the
        // entire queue to be drained before shutting it down.
        co_await tp.schedule();
        co_await pd;
        co_await q.shutdown_notify_waiters_drain(tp);
        co_return;
    };

    auto make_consumer_task = [](coro::thread_pool& tp, coro::queue<uint64_t>& q, coro::mutex& m) -> coro::task<void>
    {
        co_await tp.schedule();

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
