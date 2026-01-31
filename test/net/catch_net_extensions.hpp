#pragma once

#include "catch_amalgamated.hpp"
#include "coro/net/io_status.hpp"

struct IoStatusMatcher : Catch::Matchers::MatcherBase<coro::net::io_status>
{
    coro::net::io_status::kind expected_kind;

    explicit IoStatusMatcher(coro::net::io_status::kind k) : expected_kind(k) {}

    bool match(const coro::net::io_status& actual) const override { return actual.type == expected_kind; }

    std::string describe() const override
    {
        std::ostringstream ss;
        ss << "is io_status::kind::" << static_cast<int>(expected_kind);
        return ss.str();
    }
};

inline IoStatusMatcher IsOk()
{
    return IoStatusMatcher(coro::net::io_status::kind::ok);
}

namespace Catch
{
template<>
struct StringMaker<coro::net::io_status>
{
    static std::string convert(const coro::net::io_status& value) { return value.message(); }
};
} // namespace Catch