#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("generator", "[generator]")
{
    std::cerr << "[generator]\n\n";
}

TEST_CASE("generator single yield", "[generator]")
{
    const std::string msg{"Hello World Generator!"};
    auto              func = [](const std::string& msg) -> coro::generator<std::string> { co_yield std::string{msg}; };

    for (const auto& v : func(msg))
    {
        REQUIRE(v == msg);
    }
}

TEST_CASE("generator infinite incrementing integer yield", "[generator]")
{
    constexpr const int64_t max = 1024;

    auto func = []() -> coro::generator<int64_t>
    {
        int64_t i{0};
        while (true)
        {
            ++i;
            co_yield i;
        }
    };

    int64_t v{1};
    for (const auto& v_1 : func())
    {
        REQUIRE(v == v_1);
        ++v;

        if (v > max)
        {
            break;
        }
    }
}

TEST_CASE("generator satisfies view concept for compatibility with std::views::take", "[generator]")
{
    auto counter = size_t{0};
    auto natural = [](size_t n) mutable -> coro::generator<size_t>
    {
        while (true)
        {
            ++n;
            co_yield n;
        }
    };
    auto nat = natural(counter);
    static_assert(std::ranges::view<decltype(nat)>, "does not satisfy view concept");
    SECTION("Count the items")
    {
        for (auto&& n : natural(counter) | std::views::take(5))
        {
            ++counter;
            REQUIRE(n == counter);
        }
        REQUIRE(counter == 5);
    }
    SECTION("Not supported when std::ranges::view is satisfied, see issue 261")
    {
        /// the following may fail to compile to prevent loss of items in the std::views:take:
        /*
        for (auto&& n : nat | std::views::take(3)) {
            ++counter;
            REQUIRE(n == counter); // expect 1, 2, 3
        }
        for (auto&& n : nat | std::views::take(3)) {
            ++counter;
            REQUIRE(n == counter); // expect 4, 5, 6 (4 may get lost if view is not enabled)
        }
        */
    }
}

TEST_CASE("generator allow co_await internally", "[generator]")
{
    auto outer = []() -> coro::generator<int>
    {
        auto awaitable = []() -> coro::task<int>
        {
            co_return 5;
        };

        auto inner = []() -> coro::generator<int>
        {
            co_yield 1;
            co_yield 2;
            co_yield 3;
        };

        co_yield 100;
        co_yield 200;
        for (auto&& v : inner())
        {
            co_yield v;
        }
        co_yield co_await awaitable();
        co_yield 300;
    };

    std::size_t i{0};
    std::vector<int> expected{100, 200, 1, 2, 3, 5, 300};
    for (auto&& actual : outer())
    {
        REQUIRE(actual == expected[i++]);
    }
}

TEST_CASE("generator throws", "[generator]")
{
    std::string msg{"I always throw."};
    auto make_throwing_generator = [](std::string& msg) -> coro::generator<int>
    {
        co_yield 1;
        throw std::runtime_error{msg};
        co_yield 2;
    };

    try
    {
        for (auto&& v : make_throwing_generator(msg))
        {
            REQUIRE(v == 1);
        }
    }
    catch (std::exception& e)
    {
        REQUIRE(e.what() == msg);
    }
}

TEST_CASE("~generator", "[generator]")
{
    std::cerr << "[~generator]\n\n";
}
