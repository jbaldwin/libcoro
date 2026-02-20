#include "catch_amalgamated.hpp"
#include "catch_extensions.hpp"
#include "coro/net/socket_address.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING

    #include "catch_net_extensions.hpp"
    #include <coro/coro.hpp>

    #include <iostream>

TEST_CASE("tcp_server", "[tcp_server]")
{
    std::cerr << "[tcp_server]\n\n";
}

TEST_CASE("tcp_server nullptr scheduler", "[tcp_server]")
{
    std::unique_ptr<coro::scheduler> scheduler;
    const auto                       address = coro::net::socket_address{"127.0.0.1", 8080};

    CHECK_THROWS_AS((coro::net::tcp::client{scheduler, address}), std::runtime_error);
    CHECK_THROWS_AS((coro::net::tcp::server{scheduler, address}), std::runtime_error);
}

auto make_client_ping_task(
    std::unique_ptr<coro::scheduler>& scheduler,
    const std::string&                client_msg,
    const std::string&                server_msg,
    const coro::net::socket_address&  endpoint,
    bool                              is_exact) -> coro::task<void>
{
    co_await scheduler->schedule();
    coro::net::tcp::client client{scheduler, endpoint};

    std::cerr << "client connect()\n";
    auto cstatus = co_await client.connect();

    // should return the same status
    auto cstatus_again = co_await client.connect();
    CHECK(cstatus == cstatus_again);
    REQUIRE(cstatus == coro::net::connect_status::connected);

    std::cerr << "client write_all()\n";
    auto [sstatus, remaining] = co_await client.write_all(client_msg);
    REQUIRE_THAT(sstatus, IsOk());
    REQUIRE(remaining.empty());

    if (is_exact)
    {
        std::string buffer(server_msg.size(), '\0');
        std::cerr << "client read_exact()\n";
        auto [rstatus, rspan] = co_await client.read_exact(buffer);
        REQUIRE_THAT(rstatus, IsOk());
        REQUIRE(rspan.size() == server_msg.size());
        REQUIRE(buffer == server_msg);
    }
    else
    {
        std::string buffer(256, '\0');
        std::cerr << "client read_some()\n";
        auto [rstatus, rspan] = co_await client.read_some(buffer);
        REQUIRE_THAT(rstatus, IsOk());
        REQUIRE(rspan.size() == server_msg.length());
        buffer.resize(rspan.size());
        REQUIRE(buffer == server_msg);
    }

    std::cerr << "client return\n";
    co_return;
}

auto make_server_ping_task(
    std::unique_ptr<coro::scheduler>& scheduler,
    const std::string&                client_msg,
    const std::string&                server_msg,
    const coro::net::socket_address&  endpoint,
    bool                              is_exact) -> coro::task<void>
{
    co_await scheduler->schedule();
    coro::net::tcp::server server{scheduler, endpoint};

    std::cerr << "server accept()\n";
    auto client = co_await server.accept();
    REQUIRE(client.has_value());

    if (is_exact)
    {
        std::string buffer(client_msg.size(), '\0');
        std::cerr << "server read_exact()\n";
        auto [rstatus, rspan] = co_await client->read_exact(buffer);
        REQUIRE_THAT(rstatus, IsOk());
        REQUIRE(rspan.size() == client_msg.size());
        REQUIRE(buffer == client_msg);
    }
    else
    {
        std::string buffer(256, '\0');
        std::cerr << "server read_some()\n";
        auto [rstatus, rspan] = co_await client->read_some(buffer);
        REQUIRE_THAT(rstatus, IsOk());
        REQUIRE(rspan.size() == client_msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == client_msg);
    }

    // Respond to client.
    std::cerr << "server write_all()\n";
    auto [wstatus, remaining] = co_await client->write_all(server_msg);
    REQUIRE_THAT(wstatus, IsOk());
    REQUIRE(remaining.empty());

    // Wait a bit, so the connection doesn't get reset.
    co_await scheduler->yield_for(std::chrono::milliseconds(15));

    std::cerr << "server return\n";
    co_return;
}

TEST_CASE("tcp_server ping server", "[tcp_server]")
{
    const auto endpoint = GENERATE(
        as<coro::net::socket_address>{},
        coro::net::socket_address{"127.0.0.1", 8080},
        coro::net::socket_address{"::1", 8080, coro::net::domain_t::ipv6});

    const auto [test_name, data_size, exact] = GENERATE(
        table<std::string, size_t, bool>({
            {"small data (read_some)", 32, false},         // small enough to fit into one write/read_some
            {"large data (read_exact)", 1024 * 1024, true} // 1MB
        }));

    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    std::string client_msg(data_size, 'C');
    std::string server_msg(data_size, 'S');

    DYNAMIC_SECTION("Domain: " << to_string(endpoint.domain()) << " | " << test_name)
    {
        std::cerr << "BEGIN tcp_server, domain: " << to_string(endpoint.domain()) << " | " << test_name;

        coro::sync_wait(
            coro::when_all(
                make_server_ping_task(scheduler, client_msg, server_msg, endpoint, exact),
                make_client_ping_task(scheduler, client_msg, server_msg, endpoint, exact)));
        std::cerr << "END tcp_server\n";
    }
}

constexpr static std::size_t timeout_buf_size = 10;

auto make_server_timeout_task(
    std::unique_ptr<coro::scheduler>& scheduler,
    const coro::net::socket_address&  bound_address,
    const std::chrono::milliseconds&  timeout_duration,
    bool                              is_exact) -> coro::task<void>
{
    co_await scheduler->schedule();
    coro::net::tcp::server server{scheduler, bound_address};

    auto client_conn = co_await server.accept();
    REQUIRE(client_conn.has_value());

    if (is_exact)
    {
        // Send some bytes, but not all of them
        std::array<std::byte, timeout_buf_size> buf{};
        buf.fill(static_cast<std::byte>(0xAA));
        co_await client_conn->write_all(buf);
    }

    // Wait until client timeouts
    co_await scheduler->yield_for(timeout_duration * 2);
    co_return;
};

auto make_client_timeout_task(
    std::unique_ptr<coro::scheduler>& scheduler,
    const coro::net::socket_address&  addr,
    const std::chrono::milliseconds&  timeout_duration,
    bool                              is_exact) -> coro::task<void>
{
    co_await scheduler->schedule();
    coro::net::tcp::client client{scheduler, addr};

    auto cstatus = co_await client.connect();
    REQUIRE(cstatus == coro::net::connect_status::connected);

    if (is_exact)
    {
        std::string buffer(100, '\0');
        auto [status, rspan] = co_await client.read_exact(buffer, timeout_duration);

        CHECK(status.type == coro::net::io_status::kind::timeout);
        CHECK(rspan.size() == timeout_buf_size);
    }
    else
    {
        std::string buffer(10, '\0');
        auto [status, rspan] = co_await client.read_some(buffer, timeout_duration);

        CHECK(status.type == coro::net::io_status::kind::timeout);
        CHECK(rspan.empty());
    }

    co_return;
}

TEST_CASE("tcp_server timeout", "[tcp_server]")
{
    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    coro::net::socket_address       address{"127.0.0.1", 8080};
    const std::chrono::milliseconds timeout_duration{50};
    auto                            exact = GENERATE(true, false);

    DYNAMIC_SECTION("exact: " << exact)
    {
        coro::sync_wait(
            coro::when_all(
                make_server_timeout_task(scheduler, address, timeout_duration, exact),
                make_client_timeout_task(scheduler, address, timeout_duration, exact)));
    }
}

// This test is known to not work on kqueue style systems (e.g. apple) because the socket shutdown()
// call does not properly trigger an EV_EOF flag on the accept socket.

TEST_CASE("tcp_server graceful shutdown via socket", "[tcp_server]")
{
    std::cerr << "BEGIN tcp_server graceful shutdown via socket\n";
    auto scheduler = coro::scheduler::make_unique(
        coro::scheduler::options{.execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_inline});
    coro::net::tcp::server server{scheduler, {"127.0.0.1", 8080}};
    coro::event            started{};

    auto make_accept_task = [](coro::net::tcp::server& server, coro::event& started) -> coro::task<void>
    {
        std::cerr << "make accept task start\n";
        started.set();
        auto client = co_await server.accept();
        REQUIRE_FALSE(client.has_value());
        REQUIRE(client.error().type == coro::net::io_status::kind::cancelled);
        std::cerr << "make accept task completed\n";
    };

    scheduler->spawn_detached(make_accept_task(server, started));

    coro::sync_wait(started);
    // we'll wait a bit to make sure the server.poll() is fully registered.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    server.shutdown();

    scheduler->shutdown();
    std::cerr << "END tcp_server graceful shutdown via socket\n";
}

TEST_CASE("~tcp_server", "[tcp_server]")
{
    std::cerr << "[~tcp_server]\n\n";
}

#endif // LIBCORO_FEATURE_NETWORKING
