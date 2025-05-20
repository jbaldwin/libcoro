#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = coro::io_scheduler::make_shared();
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<uint64_t> condition{0};
    std::stop_source ss{};

    auto make_waiter_task = [](std::shared_ptr<coro::io_scheduler> scheduler, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss, std::atomic<uint64_t>& condition, int64_t id) -> coro::task<void>
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

    auto make_notifier_task = [](std::shared_ptr<coro::io_scheduler> scheduler, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss, std::atomic<uint64_t>& condition) -> coro::task<void>
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
