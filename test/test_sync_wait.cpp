#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>
#include <random>
#include <unordered_set>

TEST_CASE("sync_wait", "[sync_wait]")
{
    std::cerr << "[sync_wait]\n\n";
}

TEST_CASE("sync_wait simple integer return", "[sync_wait]")
{
    auto func = []() -> coro::task<int> { co_return 11; };

    auto result = coro::sync_wait(func());
    REQUIRE(result == 11);
}

TEST_CASE("sync_wait void", "[sync_wait]")
{
    std::string output;

    auto func = [](std::string& output) -> coro::task<void>
    {
        output = "hello from sync_wait<void>\n";
        co_return;
    };

    coro::sync_wait(func(output));
    REQUIRE(output == "hello from sync_wait<void>\n");
}

TEST_CASE("sync_wait task co_await single", "[sync_wait]")
{
    auto await_answer = []() -> coro::task<int>
    {
        auto answer = []() -> coro::task<int>
        {
            std::cerr << "\tThinking deep thoughts...\n";
            co_return 42;
        };
        std::cerr << "\tStarting to wait for answer.\n";
        auto a = answer();
        std::cerr << "\tGot the coroutine, getting the value.\n";
        auto v = co_await a;
        std::cerr << "\tCoroutine value is " << v << "\n";
        REQUIRE(v == 42);
        v = co_await a;
        std::cerr << "\tValue is still " << v << "\n";
        REQUIRE(v == 42);
        co_return 1337;
    };

    auto output = coro::sync_wait(await_answer());
    REQUIRE(output == 1337);
}

TEST_CASE("sync_wait task that throws", "[sync_wait]")
{
    auto f = []() -> coro::task<uint64_t>
    {
        throw std::runtime_error("I always throw!");
        co_return 1;
    };

    REQUIRE_THROWS(coro::sync_wait(f()));
}

TEST_CASE("sync_wait task void that throws user exception inheriting std::exception", "[sync_wait]")
{
    struct user_exception : public std::exception
    {
        user_exception(std::string what)
            : m_what(std::move(what)) { }
        auto what() const noexcept -> const char* override { return m_what.data(); }

        std::string m_what;
    };

    auto f = []() -> coro::task<void>
    {
        throw user_exception("user exception thrown");
        co_return;
    };

    try {
        coro::sync_wait(f());
    } catch (const user_exception& e) {
        REQUIRE(e.what() == std::string{"user exception thrown"});
    } catch (...) {
        FAIL("Expected user_exception");
    }
}

TEST_CASE("sync_wait task int64_t that throws user exception inheriting std::exception", "[sync_wait]")
{
    struct user_exception : public std::exception
    {
        user_exception(std::string what)
            : m_what(std::move(what)) { }
        auto what() const noexcept -> const char* override { return m_what.data(); }

        std::string m_what;
    };

    auto f = []() -> coro::task<uint64_t>
    {
        throw user_exception("user exception thrown");
        co_return 1;
    };

    try {
        coro::sync_wait(f());
    } catch (const user_exception& e) {
        REQUIRE(e.what() == std::string{"user exception thrown"});
    } catch (...) {
        FAIL("Expected user_exception");
    }
}

TEST_CASE("sync_wait very rarely hangs issue-270", "[sync_wait]")
{
    auto tp = coro::thread_pool::make_unique();

    const int ITERATIONS = 100;

    std::unordered_set<int> data{};
    data.reserve(ITERATIONS);

    std::random_device                                       dev;
    std::mt19937                                             rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, ITERATIONS);

    for (int i = 0; i < ITERATIONS; ++i)
    {
        data.insert(dist(rng));
    }

    std::atomic<int> count{0};

    auto make_task =
        [](std::unique_ptr<coro::thread_pool>& tp, std::unordered_set<int>& data, std::atomic<int>& count, int i) -> coro::task<void>
    {
        co_await tp->schedule();

        if (data.find(i) != data.end())
        {
            count.fetch_add(1);
        }

        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(ITERATIONS);
    for (int i = 0; i < ITERATIONS; ++i)
    {
        tasks.emplace_back(make_task(tp, data, count, i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(count > 0);
}

struct Foo
{
    static std::atomic<uint64_t> m_copies;
    static std::atomic<uint64_t> m_moves;
    int                          v;
    Foo() { std::cerr << "Foo::Foo()" << std::endl; }
    Foo(const Foo& other) : v(other.v)
    {
        std::cerr << "Foo::Foo(const Foo&)" << std::endl;
        m_copies.fetch_add(1);
    }
    Foo(Foo&& other) : v(std::exchange(other.v, 0))
    {
        std::cerr << "Foo::Foo(Foo&&)" << std::endl;
        m_moves.fetch_add(1);
    }

    auto operator=(const Foo& other) -> Foo&
    {
        std::cerr << "Foo::operator=(const Foo&) -> Foo&" << std::endl;
        m_copies.fetch_add(1);
        if (std::addressof(other) != this)
        {
            this->v = other.v;
        }
        return *this;
    }
    auto operator=(Foo&& other) -> Foo&
    {
        std::cerr << "Foo::operator=(Foo&&) -> Foo&" << std::endl;
        m_moves.fetch_add(1);
        if (std::addressof(other) != this)
        {
            this->v = std::exchange(other.v, 0);
        }
        return *this;
    }

    ~Foo()
    {
        std::cerr << "Foo::~Foo()"
                  << "v=" << this->v << std::endl;
    }
};

std::atomic<uint64_t> Foo::m_copies = std::atomic<uint64_t>{0};
std::atomic<uint64_t> Foo::m_moves  = std::atomic<uint64_t>{0};

TEST_CASE("issue-286", "[sync_wait]")
{
    /**
     * The expected output from this should be the follow as of writing this test.
     * https://github.com/jbaldwin/libcoro/issues/286 user @baderouaich reported
     * that libcoro compared to other coroutine libraries sync_wait equivalent had
     * and extra move.
     *
     *  Foo::Foo()
     *  co_return foo;
     *  Foo::Foo(Foo &&)
     *  Foo::~Foo()v=0
     *  Foo::Foo(Foo &&)
     *  Foo::~Foo()v=0
     *  1337
     *  Foo::~Foo()v=1337
     */

    auto getFoo = []() -> coro::task<Foo>
    {
        Foo foo{};
        foo.v = 1337;
        std::cerr << "co_return foo;" << std::endl;
        co_return foo;
    };

    auto foo = coro::sync_wait(getFoo());
    std::cerr << foo.v << std::endl;
    REQUIRE(foo.v == 1337);
    REQUIRE(foo.m_copies == 0);
    REQUIRE(foo.m_moves == 2);
}

TEST_CASE("sync_wait move-only, non-move assignable", "[sync_wait]")
{
    struct move_constructible
    {
        explicit move_constructible(int data) : data(data) {};
        move_constructible(const move_constructible&)            = delete;
        move_constructible(move_constructible&&) noexcept        = default;
        move_constructible& operator=(const move_constructible&) = delete;
        move_constructible& operator=(move_constructible&&)      = delete;
        ~move_constructible()                                    = default;

        int data{};
    };

    {
        struct move_constructible_awaitable : std::suspend_never
        {
            move_constructible await_resume() { return move_constructible(42); };
        };

        auto await_result = coro::sync_wait(move_constructible_awaitable{});

        REQUIRE(await_result.data == 42);
    }

    auto make_test = [](move_constructible value) -> coro::task<move_constructible>
    {
        co_return std::move(value);
    };

    auto result = coro::sync_wait(make_test(move_constructible(42)));

    REQUIRE(result.data == 42);
}

TEST_CASE("sync_wait copy-only, non-assignable", "[sync_wait]")
{
    struct copy_constructible
    {
        //copy_constructible() = default;
        explicit copy_constructible(int data) : data(data) {};
        copy_constructible(const copy_constructible&)            = default;
        copy_constructible(copy_constructible&&) noexcept        = delete;
        copy_constructible& operator=(const copy_constructible&) = delete;
        copy_constructible& operator=(copy_constructible&&)      = delete;
        ~copy_constructible()                                    = default;

        int data{};
    };

    {
        struct copy_constructible_awaitable : std::suspend_never
        {
            copy_constructible await_resume() { return copy_constructible(42); };
        };

        auto await_result = coro::sync_wait(copy_constructible_awaitable{});

        REQUIRE(await_result.data == 42);
    }

    {
        struct const_copy_constructible_awaitable : std::suspend_never
        {
            const copy_constructible await_resume() { return copy_constructible(42); };
        };

        auto await_result = coro::sync_wait(const_copy_constructible_awaitable{});

        REQUIRE(await_result.data == 42);
    }
}

TEST_CASE("~sync_wait", "[sync_wait]")
{
    std::cerr << "[~sync_wait]\n\n";
}
