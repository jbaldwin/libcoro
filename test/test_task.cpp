#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <thread>

TEST_CASE("task hello world", "[task]")
{
    using task_type = coro::task<std::string>;

    auto h = []() -> task_type { co_return "Hello"; }();
    auto w = []() -> task_type { co_return "World"; }();

    REQUIRE_THROWS_AS(h.promise().result(), std::runtime_error);
    REQUIRE_THROWS_AS(w.promise().result(), std::runtime_error);

    h.resume(); // task suspends immediately
    w.resume();

    REQUIRE(h.is_ready());
    REQUIRE(w.is_ready());

    auto w_value = std::move(w).promise().result();

    REQUIRE(h.promise().result() == "Hello");
    REQUIRE(w_value == "World");
    REQUIRE(w.promise().result().empty());
}

TEST_CASE("task void", "[task]")
{
    using namespace std::chrono_literals;
    using task_type = coro::task<>;

    auto t = []() -> task_type
    {
        std::this_thread::sleep_for(10ms);
        co_return;
    }();
    t.resume();

    REQUIRE(t.is_ready());
}

TEST_CASE("task exception thrown", "[task]")
{
    using task_type = coro::task<std::string>;

    std::string throw_msg = "I'll be reached";

    auto task = [](std::string& throw_msg) -> task_type
    {
        throw std::runtime_error(throw_msg);
        co_return "I'll never be reached";
    }(throw_msg);

    task.resume();

    REQUIRE(task.is_ready());

    bool thrown{false};
    try
    {
        auto value = task.promise().result();
    }
    catch (const std::exception& e)
    {
        thrown = true;
        REQUIRE(e.what() == throw_msg);
    }

    REQUIRE(thrown);
}

TEST_CASE("task in a task", "[task]")
{
    auto outer_task = []() -> coro::task<>
    {
        auto inner_task = []() -> coro::task<int>
        {
            std::cerr << "inner_task start\n";
            std::cerr << "inner_task stop\n";
            co_return 42;
        };

        std::cerr << "outer_task start\n";
        auto v = co_await inner_task();
        REQUIRE(v == 42);
        std::cerr << "outer_task stop\n";
    }();

    outer_task.resume(); // all tasks start suspend, kick it off.

    REQUIRE(outer_task.is_ready());
}

TEST_CASE("task in a task in a task", "[task]")
{
    auto task1 = []() -> coro::task<>
    {
        std::cerr << "task1 start\n";
        auto task2 = []() -> coro::task<int>
        {
            std::cerr << "\ttask2 start\n";
            auto task3 = []() -> coro::task<int>
            {
                std::cerr << "\t\ttask3 start\n";
                std::cerr << "\t\ttask3 stop\n";
                co_return 3;
            };

            auto v2 = co_await task3();
            REQUIRE(v2 == 3);

            std::cerr << "\ttask2 stop\n";
            co_return 2;
        };

        auto v1 = co_await task2();
        REQUIRE(v1 == 2);

        std::cerr << "task1 stop\n";
    }();

    task1.resume(); // all tasks start suspended, kick it off.

    REQUIRE(task1.is_ready());
}

TEST_CASE("task multiple suspends return void", "[task]")
{
    auto task = []() -> coro::task<void>
    {
        co_await std::suspend_always{};
        co_await std::suspend_never{};
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_return;
    }();

    task.resume(); // initial suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // first internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // second internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // third internal suspend
    REQUIRE(task.is_ready());
}

TEST_CASE("task multiple suspends return integer", "[task]")
{
    auto task = []() -> coro::task<int>
    {
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_return 11;
    }();

    task.resume(); // initial suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // first internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // second internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // third internal suspend
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().result() == 11);
}

TEST_CASE("task resume from promise to coroutine handles of different types", "[task]")
{
    auto task1 = [&]() -> coro::task<int>
    {
        std::cerr << "Task ran\n";
        co_return 42;
    }();

    auto task2 = [&]() -> coro::task<void>
    {
        std::cerr << "Task 2 ran\n";
        co_return;
    }();

    // task.resume();  normal method of resuming

    std::vector<std::coroutine_handle<>> handles;

    handles.emplace_back(std::coroutine_handle<coro::task<int>::promise_type>::from_promise(task1.promise()));
    handles.emplace_back(std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task2.promise()));

    auto& coro_handle1 = handles[0];
    coro_handle1.resume();
    auto& coro_handle2 = handles[1];
    coro_handle2.resume();

    REQUIRE(task1.is_ready());
    REQUIRE(coro_handle1.done());
    REQUIRE(task1.promise().result() == 42);

    REQUIRE(task2.is_ready());
    REQUIRE(coro_handle2.done());
}

TEST_CASE("task throws void", "[task]")
{
    auto task = []() -> coro::task<void>
    {
        throw std::runtime_error{"I always throw."};
        co_return;
    }();

    REQUIRE_NOTHROW(task.resume());
    REQUIRE(task.is_ready());
    REQUIRE_THROWS_AS(task.promise().result(), std::runtime_error);
}

TEST_CASE("task throws non-void l-value", "[task]")
{
    auto task = []() -> coro::task<int>
    {
        throw std::runtime_error{"I always throw."};
        co_return 42;
    }();

    REQUIRE_NOTHROW(task.resume());
    REQUIRE(task.is_ready());
    REQUIRE_THROWS_AS(task.promise().result(), std::runtime_error);
}

TEST_CASE("task throws non-void r-value", "[task]")
{
    struct type
    {
        int m_value;
    };

    auto task = []() -> coro::task<type>
    {
        type return_value{42};

        throw std::runtime_error{"I always throw."};
        co_return std::move(return_value);
    }();

    task.resume();
    REQUIRE(task.is_ready());
    REQUIRE_THROWS_AS(task.promise().result(), std::runtime_error);
}

TEST_CASE("const task returning a reference", "[task]")
{
    struct type
    {
        int m_value;
    };

    type return_value{42};

    auto task = [](type& return_value) -> coro::task<const type&> { co_return std::ref(return_value); }(return_value);

    task.resume();
    REQUIRE(task.is_ready());
    auto& result = task.promise().result();
    REQUIRE(result.m_value == 42);
    REQUIRE(std::addressof(return_value) == std::addressof(result));
    static_assert(std::is_same_v<decltype(task.promise().result()), const type&>);
}

TEST_CASE("mutable task returning a reference", "[task]")
{
    struct type
    {
        int m_value;
    };

    type return_value{42};

    auto task = [](type& return_value) -> coro::task<type&> { co_return std::ref(return_value); }(return_value);

    task.resume();
    REQUIRE(task.is_ready());
    auto& result = task.promise().result();
    REQUIRE(result.m_value == 42);
    REQUIRE(std::addressof(return_value) == std::addressof(result));
    static_assert(std::is_same_v<decltype(task.promise().result()), type&>);
}

TEST_CASE("task doesn't require default constructor", "[task]")
{
    // https://github.com/jbaldwin/libcoro/issues/163
    // Reported issue that the return type required a default constructor.
    // This test explicitly creates an object that does not have a default
    // constructor to verify that the default constructor isn't required.

    struct A
    {
        A(int value) : m_value(value) {}

        int m_value{};
    };

    auto make_task = []() -> coro::task<A> { co_return A(42); };

    REQUIRE(coro::sync_wait(make_task()).m_value == 42);
}

TEST_CASE("task supports instantiation with rvalue reference", "[task]")
{
    // https://github.com/jbaldwin/libcoro/issues/180
    // Reported issue that the return type cannot be rvalue reference.
    // This test explicitly creates an coroutine that returns a task
    // instantiated with rvalue reference to verify that rvalue
    // reference is supported.

    int  i         = 42;
    auto make_task = [&i]() -> coro::task<int&&> { co_return std::move(i); };
    int  ret       = coro::sync_wait(make_task());
    REQUIRE(ret == 42);
}