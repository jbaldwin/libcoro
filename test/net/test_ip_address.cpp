#include "catch_amalgamated.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING

    #include <coro/coro.hpp>

    #include <chrono>
    #include <iomanip>

TEST_CASE("net::ip_address from_string() ipv4")
{
    {
        auto ip_addr = coro::net::ip_address::from_string("127.0.0.1");
        REQUIRE(ip_addr.to_string() == "127.0.0.1");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv4);
        std::array<uint8_t, coro::net::ip_address::ipv4_len> expected{127, 0, 0, 1};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }

    {
        auto ip_addr = coro::net::ip_address::from_string("255.255.0.0");
        REQUIRE(ip_addr.to_string() == "255.255.0.0");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv4);
        std::array<uint8_t, coro::net::ip_address::ipv4_len> expected{255, 255, 0, 0};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }
}

TEST_CASE("net::ip_address from_string() ipv6")
{
    {
        auto ip_addr =
            coro::net::ip_address::from_string("0123:4567:89ab:cdef:0123:4567:89ab:cdef", coro::net::domain_t::ipv6);
        REQUIRE(ip_addr.to_string() == "123:4567:89ab:cdef:123:4567:89ab:cdef");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv6);
        std::array<uint8_t, coro::net::ip_address::ipv6_len> expected{
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }

    {
        auto ip_addr = coro::net::ip_address::from_string("::", coro::net::domain_t::ipv6);
        REQUIRE(ip_addr.to_string() == "::");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv6);
        std::array<uint8_t, coro::net::ip_address::ipv6_len> expected{};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }

    {
        auto ip_addr = coro::net::ip_address::from_string("::1", coro::net::domain_t::ipv6);
        REQUIRE(ip_addr.to_string() == "::1");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv6);
        std::array<uint8_t, coro::net::ip_address::ipv6_len> expected{
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }

    {
        auto ip_addr = coro::net::ip_address::from_string("1::1", coro::net::domain_t::ipv6);
        REQUIRE(ip_addr.to_string() == "1::1");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv6);
        std::array<uint8_t, coro::net::ip_address::ipv6_len> expected{
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }

    {
        auto ip_addr = coro::net::ip_address::from_string("1::", coro::net::domain_t::ipv6);
        REQUIRE(ip_addr.to_string() == "1::");
        REQUIRE(ip_addr.domain() == coro::net::domain_t::ipv6);
        std::array<uint8_t, coro::net::ip_address::ipv6_len> expected{
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        REQUIRE(std::equal(expected.begin(), expected.end(), ip_addr.data().begin()));
    }
}

#endif //# LIBCORO_FEATURE_NETWORKING