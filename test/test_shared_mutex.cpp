#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

TEST_CASE("mutex single waiter not locked exclusive", "[shared_mutex]")
{
    auto                  tp = std::make_shared<coro::thread_pool>(coro::thread_pool::options{.thread_count = 1});
    std::vector<uint64_t> output;

    coro::shared_mutex<coro::thread_pool> m{tp};

    auto make_emplace_task = [&](coro::shared_mutex<coro::thread_pool>& m) -> coro::task<void>
    {
        std::cerr << "Acquiring lock exclusive\n";
        {
            auto scoped_lock = co_await m.lock();
            REQUIRE_FALSE(m.try_lock());
            REQUIRE_FALSE(m.try_lock_shared());
            std::cerr << "lock acquired, emplacing back 1\n";
            output.emplace_back(1);
            std::cerr << "coroutine done\n";
        }

        // The scoped lock should release the lock upon destructing.
        REQUIRE(m.try_lock());
        m.unlock();

        co_return;
    };

    coro::sync_wait(make_emplace_task(m));

    REQUIRE(m.try_lock());
    m.unlock();

    REQUIRE(output.size() == 1);
    REQUIRE(output[0] == 1);
}

TEST_CASE("mutex single waiter not locked shared", "[shared_mutex]")
{
    auto                  tp = std::make_shared<coro::thread_pool>(coro::thread_pool::options{.thread_count = 1});
    std::vector<uint64_t> values{1, 2, 3};

    coro::shared_mutex<coro::thread_pool> m{tp};

    auto make_emplace_task = [&](coro::shared_mutex<coro::thread_pool>& m) -> coro::task<void>
    {
        std::cerr << "Acquiring lock shared\n";
        {
            auto scoped_lock = co_await m.lock_shared();
            REQUIRE_FALSE(m.try_lock());
            REQUIRE(m.try_lock_shared());
            std::cerr << "lock acquired, reading values\n";
            for (const auto& v : values)
            {
                std::cerr << v << ",";
            }
            std::cerr << "\ncoroutine done\n";

            m.unlock_shared(); // manually locked shared on a shared, unlock
        }

        // The scoped lock should release the lock upon destructing.
        REQUIRE(m.try_lock());
        m.unlock();

        co_return;
    };

    coro::sync_wait(make_emplace_task(m));

    REQUIRE(m.try_lock_shared());
    m.unlock_shared();

    REQUIRE(m.try_lock());
    m.unlock();
}

#ifdef LIBCORO_FEATURE_THREADING
TEST_CASE("mutex many shared and exclusive waiters interleaved", "[shared_mutex]")
{
    auto tp = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 8}});
    coro::shared_mutex<coro::io_scheduler> m{tp};

    std::atomic<bool> read_value{false};

    auto make_shared_task = [&]() -> coro::task<bool>
    {
        co_await tp->schedule();
        std::cerr << "make_shared_task shared lock acquiring\n";
        auto scoped_lock = co_await m.lock_shared();
        std::cerr << "make_shared_task shared lock acquired\n";
        bool value = read_value.load(std::memory_order::acquire);
        std::cerr << "make_shared_task shared lock releasing on thread_id = " << std::this_thread::get_id() << "\n";
        co_return value;
    };

    auto make_exclusive_task = [&]() -> coro::task<void>
    {
        // Let some readers get through.
        co_await tp->yield_for(std::chrono::milliseconds{50});

        {
            std::cerr << "make_shared_task exclusive lock acquiring\n";
            auto scoped_lock = co_await m.lock();
            std::cerr << "make_shared_task exclusive lock acquired\n";
            // Stack readers on the mutex
            co_await tp->yield_for(std::chrono::milliseconds{50});
            read_value.exchange(true, std::memory_order::release);
            std::cerr << "make_shared_task exclusive lock releasing\n";
        }

        co_return;
    };

    auto make_shared_tasks_task = [&]() -> coro::task<void>
    {
        co_await tp->schedule();

        std::vector<coro::task<bool>> shared_tasks{};

        bool stop{false};
        while (!stop)
        {
            shared_tasks.emplace_back(make_shared_task());
            shared_tasks.back().resume();

            co_await tp->yield_for(std::chrono::milliseconds{1});

            for (const auto& st : shared_tasks)
            {
                if (st.is_ready())
                {
                    stop = st.promise().result();
                }
            }
        }

        while (true)
        {
            bool tasks_remaining{false};
            for (const auto& st : shared_tasks)
            {
                if (!st.is_ready())
                {
                    tasks_remaining = true;
                    break;
                }
            }

            if (!tasks_remaining)
            {
                break;
            }
        }

        co_return;
    };

    coro::sync_wait(coro::when_all(make_shared_tasks_task(), make_exclusive_task()));
}
#endif
