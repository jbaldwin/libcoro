#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("concepts", "[concepts]")
{
    std::cerr << "[concepts]\n\n";
}

struct char_buffer
{
    bool        empty() const { return false; }
    const char* data() const { return nullptr; }
    std::size_t size() const { return 0; }
    char*       data() { return nullptr; }
};

struct uint8_buffer
{
    bool           empty() const { return false; }
    const uint8_t* data() const { return nullptr; }
    std::size_t    size() const { return 0; }
    uint8_t*       data() { return nullptr; }
};

struct std_byte_buffer
{
    bool             empty() const { return false; }
    const std::byte* data() const { return nullptr; }
    std::size_t      size() const { return 0; }
    std::byte*       data() { return nullptr; }
};

using coro::concepts::const_buffer;
using coro::concepts::mutable_buffer;

TEST_CASE("mutable_buffer", "[concepts]")
{
    static_assert(mutable_buffer<char_buffer>);
    static_assert(mutable_buffer<uint8_buffer>);
    static_assert(mutable_buffer<std_byte_buffer>);
    static_assert(mutable_buffer<std::vector<char>>);
    static_assert(mutable_buffer<std::vector<uint8_t>>);

    REQUIRE(true);
}

TEST_CASE("const_buffer", "[concepts]")
{
    static_assert(const_buffer<char_buffer>);
    static_assert(const_buffer<uint8_buffer>);
    static_assert(const_buffer<std_byte_buffer>);
    static_assert(const_buffer<std::vector<char>>);
    static_assert(const_buffer<std::vector<uint8_t>>);

    REQUIRE(true);
}

TEST_CASE("~concepts", "[concepts]")
{
    std::cerr << "[~concepts]\n\n";
}