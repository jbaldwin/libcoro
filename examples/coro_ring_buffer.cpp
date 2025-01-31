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

    auto make_producer_task =
        [](coro::thread_pool& tp, coro::ring_buffer<uint64_t, 16>& rb, coro::mutex& m) -> coro::task<void>
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

    auto make_consumer_task =
        [](coro::thread_pool& tp, coro::ring_buffer<uint64_t, 16>& rb, coro::mutex& m, size_t id) -> coro::task<void>
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
        tasks.emplace_back(make_consumer_task(tp, rb, m, i));
    }
    // Create 1 producer.
    tasks.emplace_back(make_producer_task(tp, rb, m));

    // Wait for all the values to be produced and consumed through the ring buffer.
    coro::sync_wait(coro::when_all(std::move(tasks)));
}
