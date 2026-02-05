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
        ss << "is " << to_string(expected_kind);
        return ss.str();
    }
};

inline IoStatusMatcher IsOk()
{
    return IoStatusMatcher(coro::net::io_status::kind::ok);
}
inline IoStatusMatcher IsError(coro::net::io_status::kind kind)
{
    return IoStatusMatcher(kind);
}


namespace Catch
{
template<>
struct StringMaker<coro::net::io_status>
{
    static std::string convert(const coro::net::io_status& value) { return value.message(); }
};
} // namespace Catch