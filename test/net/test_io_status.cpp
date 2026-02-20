#include "catch_amalgamated.hpp"
#include "coro/net/io_status.hpp"
#include <cerrno>

TEST_CASE("Mapping native codes", "[io_status]")
{
    auto [input_code, expected_kind] = GENERATE(
        table<int, coro::net::io_status::kind>({
            {0, coro::net::io_status::kind::ok},
            {EOF, coro::net::io_status::kind::closed},
            {ECONNREFUSED, coro::net::io_status::kind::connection_refused},
            {ECONNRESET, coro::net::io_status::kind::connection_reset},
            {EAGAIN, coro::net::io_status::kind::would_block_or_try_again},
            {EMSGSIZE, coro::net::io_status::kind::message_too_big},
            {123456, coro::net::io_status::kind::native} // Covers default case
        }));

    auto status = coro::net::make_io_status_from_native(input_code);

    CHECK(status.type == expected_kind);
    CHECK(status.native_code == input_code);
}

TEST_CASE("Mapping poll_status", "[io_status]")
{
    using ps = coro::poll_status;
    using k  = coro::net::io_status::kind;

    auto [input_status, expected_kind] = GENERATE(
        table<ps, k>(
            {{ps::read, k::ok},
             {ps::write, k::ok},
             {ps::timeout, k::timeout},
             {ps::error, k::polling_error},
             {ps::closed, k::closed},
             {ps::cancelled, k::cancelled}}));

    CHECK(coro::net::make_io_status_from_poll_status(input_status).type == expected_kind);
}