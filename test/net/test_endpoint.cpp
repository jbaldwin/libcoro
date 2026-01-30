#include "catch_amalgamated.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING

    #include <coro/coro.hpp>

TEST_CASE("net::socket_address", "[socket_address]")
{
    using namespace coro::net;

    SECTION("IPv4 construction")
    {
        const std::string ip_str = "127.0.0.1";
        const uint16_t    port   = 8080;

        socket_address ep(ip_str, port, domain_t::ipv4);

        CHECK(ep.domain() == domain_t::ipv4);
        CHECK(ep.port() == port);
        CHECK(ep.ip().to_string() == ip_str);

        auto [addr, len] = ep.data();
        REQUIRE(len == sizeof(sockaddr_in));

        const auto* sin = reinterpret_cast<const sockaddr_in*>(addr);
        CHECK(sin->sin_family == AF_INET);
        CHECK(sin->sin_port == htons(port));
        CHECK(sin->sin_addr.s_addr == inet_addr(ip_str.c_str()));
    }

    SECTION("IPv6 construction")
    {
        const std::string ip_str = "::1";
        const uint16_t    port   = 443;

        socket_address ep(ip_str, port, domain_t::ipv6);

        CHECK(ep.domain() == domain_t::ipv6);
        CHECK(ep.port() == port);

        auto [addr, len] = ep.data();
        REQUIRE(len == sizeof(sockaddr_in6));

        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(addr);
        CHECK(sin6->sin6_family == AF_INET6);
        CHECK(sin6->sin6_port == htons(port));

        CHECK(memcmp(&sin6->sin6_addr, &in6addr_loopback, sizeof(in6_addr)) == 0);
    }

    SECTION("Common operators")
    {
        socket_address ep1("192.168.0.1", 9000);
        socket_address ep2("192.168.0.1", 9000);
        socket_address ep3("192.168.0.1", 9001);
        socket_address ep4("10.0.0.1", 9000);

        SECTION("Equality operator") {
            CHECK(ep1 == ep2);
            CHECK_FALSE(ep1 == ep3);
            CHECK_FALSE(ep1 == ep4);
        }

        SECTION("to_string") {
            CHECK(ep1.to_string() == "192.168.0.1:9000");
            CHECK(ep2.to_string() == "192.168.0.1:9000");
            CHECK(ep3.to_string() == "192.168.0.1:9001");
            CHECK(ep4.to_string() == "10.0.0.1:9000");
        }
    }

    SECTION("native_mutable_data (for accept/getsockname)")
    {
        auto ep              = socket_address::make_uninitialised();
        auto [addr, len_ptr] = ep.native_mutable_data();

        REQUIRE(addr != nullptr);
        REQUIRE(len_ptr != nullptr);

        sockaddr_in* sin     = reinterpret_cast<sockaddr_in*>(addr);
        sin->sin_family      = AF_INET;
        sin->sin_port        = htons(1234);
        sin->sin_addr.s_addr = inet_addr("1.1.1.1");
        *len_ptr             = sizeof(sockaddr_in);

        CHECK(ep.domain() == domain_t::ipv4);
        CHECK(ep.port() == 1234);
        CHECK(ep.ip().to_string() == "1.1.1.1");
    }

    SECTION("Invalid domain handling/Uninitialised")
    {
        auto ep = socket_address::make_uninitialised();

        CHECK_THROWS_AS(ep.domain(), std::runtime_error);
        CHECK_THROWS_AS(ep.port(), std::runtime_error);
        CHECK_THROWS_AS(ep.ip(), std::runtime_error);
    }
}
#endif