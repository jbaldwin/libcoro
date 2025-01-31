#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

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

TEST_CASE("generator satisfies view concept for compatibility with std::views::take")
{
    auto counter = size_t{0};
    auto natural = [](size_t n) mutable -> coro::generator<size_t>
    {
        while (true)
            co_yield ++n;
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
