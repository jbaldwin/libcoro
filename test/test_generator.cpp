#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

TEST_CASE("generator single yield", "[generator]")
{
    std::string msg{"Hello World Generator!"};
    auto        func = [&]() -> coro::generator<std::string> { co_yield msg; };

    for (const auto& v : func())
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
