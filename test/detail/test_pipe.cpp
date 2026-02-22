#include "catch_amalgamated.hpp"

#include "coro/detail/pipe.hpp"

#include <iostream>

TEST_CASE("detail::pipe", "[detail::pipe]")
{
    std::cerr << "[detail::pipe]\n\n";
}

TEST_CASE("hello world", "[detail::pipe]")
{
    std::string data = "hello world hello world hello world";
    coro::detail::pipe_t p{};

    REQUIRE(p.write(data.data(), data.size()) == static_cast<long>(data.size()));
    std::string buffer{};
    buffer.resize(128);
    REQUIRE(p.read(buffer.data(), 128) == static_cast<long>(data.size()));
    buffer.resize(data.size());
    REQUIRE(data == buffer);
}

struct pipe_test_class
{
    std::string str{};
};

TEST_CASE("pass pointer", "[detail::pipe]")
{
    pipe_test_class data{};
    data.str = "hello world";
    pipe_test_class* data_ptr = &data;

    coro::detail::pipe_t p{};
    REQUIRE(p.write(&data_ptr, sizeof(pipe_test_class*)) == (long)sizeof(pipe_test_class*));

    pipe_test_class* ptr{nullptr};
    REQUIRE(p.read(static_cast<void*>(&ptr), sizeof(pipe_test_class*)) == (long)sizeof(pipe_test_class*));
    REQUIRE(ptr == &data);
    REQUIRE(ptr->str == data.str);
}

TEST_CASE("pass pointers", "[detail::pipe]")
{
    pipe_test_class data{};
    data.str = "hello world";
    pipe_test_class* data_ptr = &data;

    auto pointer_size = sizeof(pipe_test_class*);

    coro::detail::pipe_t p{};
    REQUIRE(p.write(&data_ptr, sizeof(pipe_test_class*)) == (long)pointer_size);

    std::array<pipe_test_class*, 16> tests{};
    REQUIRE(p.read(static_cast<void*>(tests.data()), 16 * pointer_size) == (long)pointer_size);
    REQUIRE(tests[0] == &data);
}

TEST_CASE("~detail::pipe", "[detail::pipe]")
{
    std::cerr << "[~detail::pipe]\n\n";
}
