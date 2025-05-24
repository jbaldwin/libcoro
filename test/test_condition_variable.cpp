#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>
#include <iostream>

TEST_CASE("wait(lock) 1 waiter", "[condition_variable]")
{
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::event e{}; // just used to coordinate the order in which the tasks run.

    auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, coro::event& e) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        e.set(); // trigger the notifier that we are waiting now that we have control of the lock
        co_await cv.wait(lk);
        co_return 42;
    };

    auto make_notifier = [](coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await e;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, e), make_notifier(cv, e)));
    REQUIRE(std::get<0>(results).return_value() == 42);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait(lock predicate) 1 waiter", "[condition_variable]")
{
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::event e{}; // just used to coordinate the order in which the tasks run.

    auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, coro::event& e) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        e.set(); // trigger the notifier that we are waiting now that we have control of the lock
        co_await cv.wait(lk, []() -> bool { return true; });
        co_return 42;
    };

    auto make_notifier = [](coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await e;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, e), make_notifier(cv, e)));
    REQUIRE(std::get<0>(results).return_value() == 42);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

#ifndef EMSCRIPTEN

TEST_CASE("wait(lock stop_token predicate) 1 waiter", "[condition_variable]")
{
    coro::condition_variable cv{};
    coro::mutex m{};
    std::stop_source ss{};

    auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        auto result = co_await cv.wait(lk, ss.get_token(), [&ss]() -> bool { return false; });
        REQUIRE(result == false);
        co_return 42;
    };

    auto make_notifier = [](coro::condition_variable& cv, std::stop_source& ss) -> coro::task<int64_t>
    {
        ss.request_stop();
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, ss), make_notifier(cv, ss)));
    REQUIRE(std::get<0>(results).return_value() == 42);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

#endif

#ifdef LIBCORO_FEATURE_NETWORKING

TEST_CASE("wait(lock predicate) 1 waiter notify_one until predicate passes", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> counter{0};
    std::atomic<int64_t> predicate_called{0};

    auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& counter, std::atomic<int64_t>& predicate_called) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        co_await cv.wait(lk, [&counter, &predicate_called]() -> bool
        {
            predicate_called++;
            return counter == 1;
        });
        co_return 42;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<int64_t>& counter) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        co_await cv.notify_one(); // The predicate will not pass
        counter++;
        co_await cv.notify_one(); // The predicate will pass
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, counter, predicate_called), make_notifier(s, cv, counter)));
    REQUIRE(std::get<0>(results).return_value() == 42);
    REQUIRE(std::get<1>(results).return_value() == 0);

    // The predicate is called 3 times
    // 1) On the initial cv.wait() to see if its already satisfied.
    // 2) On the first notify_all() when counter is still 0.
    // 3) On the final notify_all() when the counter is now 1.
    REQUIRE(predicate_called == 3);
}

TEST_CASE("wait(lock predicate) 1 waiter predicate notify_all until predicate passes", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> counter{0};
    std::atomic<int64_t> predicate_called{0};

    auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& counter, std::atomic<int64_t>& predicate_called) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        co_await cv.wait(lk, [&counter, &predicate_called]() -> bool
        {
            predicate_called++;
            return counter == 1;
        });
        co_return 42;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<int64_t>& counter) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        co_await cv.notify_all(); // The predicate will not pass
        counter++;
        co_await cv.notify_all(); // The predicate will pass
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, counter, predicate_called), make_notifier(s, cv, counter)));
    REQUIRE(std::get<0>(results).return_value() == 42);
    REQUIRE(std::get<1>(results).return_value() == 0);

    // The predicate is called 3 times
    // 1) On the initial cv.wait() to see if its already satisfied.
    // 2) On the first notify_all() when counter is still 0.
    // 3) On the final notify_all() when the counter is now 1.
    REQUIRE(predicate_called == 3);
}

TEST_CASE("wait(lock) 3 waiters notify_one", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::event e1{};
    coro::event e2{};
    coro::event e3{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, coro::event& e, int64_t r) -> coro::task<int64_t>
    {
        co_await s->schedule();
        auto lk = co_await m.scoped_lock();
        e.set();
        co_await cv.wait(lk);
        co_return r;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule_after(std::chrono::milliseconds{10});
        co_await e;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(
        make_waiter(s, cv, m, e1, 1),
        make_waiter(s, cv, m, e2, 2),
        make_waiter(s, cv, m, e3, 3),
        make_notifier(s, cv, e1),
        make_notifier(s, cv, e1),
        make_notifier(s, cv, e1)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 2);
    REQUIRE(std::get<2>(results).return_value() == 3);
}

TEST_CASE("wait(lock predicate) 3 waiters predicate notify_one", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> e{0};

    auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& e, int64_t r) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        co_await cv.wait(lk, [&e, &r]() -> bool
        {
            return e > 0;
        });
        e--;
        co_return r;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& e) -> coro::task<int64_t>
    {
        co_await s->schedule_after(std::chrono::milliseconds{10});
        {
            auto lk = co_await m.scoped_lock();
            e++;
        }
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(
        make_waiter(cv, m, e, 1),
        make_waiter(cv, m, e, 2),
        make_waiter(cv, m, e, 3),
        make_notifier(s, cv, m, e),
        make_notifier(s, cv, m, e),
        make_notifier(s, cv, m, e)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 2);
    REQUIRE(std::get<2>(results).return_value() == 3);
}

TEST_CASE("wait(lock) 3 waiters notify_all", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::latch l{3};
    coro::event e{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, coro::latch& l, int64_t r) -> coro::task<int64_t>
    {
        co_await s->schedule();
        auto lk = co_await m.scoped_lock();
        l.count_down();
        co_await cv.wait(lk);
        co_return r;
    };

    auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule();
        co_await l;
        e.set();
        co_return 0;
    };

    auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await e;
        co_await s->schedule_after(std::chrono::milliseconds{10});
        co_await cv.notify_all();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(
        make_waiter(s, cv, m, l, 1),
        make_waiter(s, cv, m, l, 2),
        make_waiter(s, cv, m, l, 3),
        make_all_waiting(s, l, e),
        make_notify_all(s, cv, e)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 2);
    REQUIRE(std::get<2>(results).return_value() == 3);
}

TEST_CASE("wait(lock predicate) 3 waiters predicate notify_all", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::latch l{3};
    coro::event e{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, coro::latch& l, int64_t r) -> coro::task<int64_t>
    {
        int64_t called{0};
        co_await s->schedule();
        auto lk = co_await m.scoped_lock();
        l.count_down();
        co_await cv.wait(lk, [&called]() -> bool { return ++called > 0; });
        co_return r;
    };

    auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule();
        co_await l;
        e.set();
        co_return 0;
    };

    auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await e;
        co_await s->schedule_after(std::chrono::milliseconds{10});
        co_await cv.notify_all();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(
        make_waiter(s, cv, m, l, 1),
        make_waiter(s, cv, m, l, 2),
        make_waiter(s, cv, m, l, 3),
        make_all_waiting(s, l, e),
        make_notify_all(s, cv, e)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 2);
    REQUIRE(std::get<2>(results).return_value() == 3);
}

TEST_CASE("wait_for(s lock duration predicate) 1 waiter predicate notify_one until predicate passes", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> counter{0};
    std::atomic<int64_t> predicate_called{0};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& counter, std::atomic<int64_t>& predicate_called) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{50}, [&counter, &predicate_called]() -> bool
        {
            predicate_called++;
            return counter == 1;
        });
        REQUIRE(status == true);
        co_return 42;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<int64_t>& counter) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        co_await cv.notify_one(); // The predicate will not pass
        counter++;
        co_await cv.notify_one(); // The predicate will pass
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, counter, predicate_called), make_notifier(s, cv, counter)));
    REQUIRE(std::get<0>(results).return_value() == 42);
    REQUIRE(std::get<1>(results).return_value() == 0);

    // The predicate is called 3 times
    // 1) On the initial cv.wait() to see if its already satisfied.
    // 2) On the first notify_all() when counter is still 0.
    // 3) On the final notify_all() when the counter is now 1.
    REQUIRE(predicate_called == 3);
}

TEST_CASE("wait_for(s lock duration) 1 waiter no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{50});
        REQUIRE(status == std::cv_status::no_timeout);
        co_return (status == std::cv_status::no_timeout) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_for(s lock duration predicate) 1 waiter predicate no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> c{0};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& c) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{50}, [&c]() -> bool { return c == 1; });
        REQUIRE(status == true);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<int64_t>& c) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        c++;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_for(s lock stop_token duration predicate) 1 waiter predicate stop_token no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::stop_source ss{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss) -> coro::task<int64_t>
    {

        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_for(s, lk, ss.get_token(), std::chrono::milliseconds{50}, [&ss]() -> bool { return ss.stop_requested(); });
        REQUIRE(status == true);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::stop_source& ss) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        ss.request_stop();
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, ss), make_notifier(s, cv, ss)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}


TEST_CASE("wait_for(s lock duration) 1 waiter timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{10});
        REQUIRE(status == std::cv_status::timeout);
        co_return (status == std::cv_status::no_timeout) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{100});
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
    REQUIRE(std::get<0>(results).return_value() == -1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_for(s lock duration predicate) 1 waiter predicate timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<uint64_t> c{0};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::atomic<uint64_t>& c) -> coro::task<int64_t>
    {
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{10}, [&c]() -> bool { return c == 1; });
        REQUIRE(status == false);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<uint64_t>& c) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{50});
        c++;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
    REQUIRE(std::get<0>(results).return_value() == -1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_for(s lock duration) 3 waiters with timeout notify_all no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::latch l{3};
    coro::event e{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, coro::latch& l, int64_t r) -> coro::task<int64_t>
    {
        co_await s->schedule();
        auto lk = co_await m.scoped_lock();
        l.count_down();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{20});
        co_return (status == std::cv_status::no_timeout) ? r : -r;
    };

    auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule();
        co_await l;
        e.set();
        co_return 0;
    };

    auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule();
        co_await e;
        co_await cv.notify_all();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(
        make_waiter(s, cv, m, l, 1),
        make_waiter(s, cv, m, l, 2),
        make_waiter(s, cv, m, l, 3),
        make_all_waiting(s, l, e),
        make_notify_all(s, cv, e)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 2);
    REQUIRE(std::get<2>(results).return_value() == 3);
}

TEST_CASE("wait_for(s lock duration) 3 with notify_all timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    coro::latch l{3};
    coro::event e{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, coro::latch& l, int64_t r) -> coro::task<int64_t>
    {
        co_await s->schedule();
        auto lk = co_await m.scoped_lock();
        l.count_down();
        auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{10});
        co_return (status == std::cv_status::no_timeout) ? r : -r;
    };

    auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule();
        co_await l;
        e.set();
        co_return 0;
    };

    auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
    {
        co_await s->schedule();
        co_await e;
        co_await s->yield_for(std::chrono::milliseconds{50});
        co_await cv.notify_all();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(
        make_waiter(s, cv, m, l, 1),
        make_waiter(s, cv, m, l, 2),
        make_waiter(s, cv, m, l, 3),
        make_all_waiting(s, l, e),
        make_notify_all(s, cv, e)));
    REQUIRE(std::get<0>(results).return_value() == -1);
    REQUIRE(std::get<1>(results).return_value() == -2);
    REQUIRE(std::get<2>(results).return_value() == -3);
}

TEST_CASE("wait_until(s lock time_point) 1 waiter no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) -> coro::task<int64_t>
    {
        auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_until(s, lk, tp);
        REQUIRE(status == std::cv_status::no_timeout);
        co_return (status == std::cv_status::no_timeout) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_until(s lock time_point) 1 waiter timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) -> coro::task<int64_t>
    {
        auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{10};
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_until(s, lk, tp);
        REQUIRE(status == std::cv_status::timeout);
        co_return (status == std::cv_status::no_timeout) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{50});
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
    REQUIRE(std::get<0>(results).return_value() == -1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_until(s lock time_point predicate) 1 waiter predicate no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> c{0};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& c) -> coro::task<int64_t>
    {
        auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_until(s, lk, tp, [&c]() -> bool { return c == 1; });
        REQUIRE(status == true);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<int64_t>& c) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        c++;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_until(s lock time_point predicate) 1 waiter predicate timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::atomic<int64_t> c{0};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& c) -> coro::task<int64_t>
    {
        auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{10};
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_until(s, lk, tp, [&c]() -> bool { return c == 1; });
        REQUIRE(status == false);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::atomic<int64_t>& c) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{50});
        c++;
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
    REQUIRE(std::get<0>(results).return_value() == -1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_until(s lock stop_token time_point predicate) 1 waiter predicate stop_token no_timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::stop_source ss{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss) -> coro::task<int64_t>
    {
        auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_until(s, lk, ss.get_token(), tp, [&ss]() -> bool { return ss.stop_requested(); });
        REQUIRE(status == true);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::stop_source& ss) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{10});
        ss.request_stop();
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, ss), make_notifier(s, cv, ss)));
    REQUIRE(std::get<0>(results).return_value() == 1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("wait_until(s lock stop_token time_point predicate) 1 waiter predicate stop_token timeout", "[condition_variable]")
{
    auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::condition_variable cv{};
    coro::mutex m{};
    std::stop_source ss{};

    auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss) -> coro::task<int64_t>
    {
        auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{10};
        auto lk = co_await m.scoped_lock();
        auto status = co_await cv.wait_until(s, lk, ss.get_token(), tp, [&ss]() -> bool { return ss.stop_requested(); });
        REQUIRE(status == false);
        co_return (status) ? 1 : -1;
    };

    auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::stop_source& ss) -> coro::task<int64_t>
    {
        co_await s->yield_for(std::chrono::milliseconds{50});
        ss.request_stop();
        co_await cv.notify_one();
        co_return 0;
    };

    auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, ss), make_notifier(s, cv, ss)));
    REQUIRE(std::get<0>(results).return_value() == -1);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

#endif
