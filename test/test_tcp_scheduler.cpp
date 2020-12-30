#include "catch.hpp"

#include <coro/coro.hpp>

TEST_CASE("tcp_scheduler no on connection throws")
{
    REQUIRE_THROWS(coro::tcp_scheduler{coro::tcp_scheduler::options{.on_connection = nullptr}});
}

TEST_CASE("tcp_scheduler echo server ip address")
{
    const std::string msg{"Hello from client"};

    auto on_connection = [&msg](coro::tcp_scheduler& scheduler, coro::net::socket sock) -> coro::task<void> {
        std::string in(64, '\0');

        auto [rstatus, rbytes] = co_await scheduler.read(sock, std::span<char>{in.data(), in.size()});
        REQUIRE(rstatus == coro::poll_status::event);

        in.resize(rbytes);
        REQUIRE(in == msg);

        auto [wstatus, wbytes] = co_await scheduler.write(sock, std::span<const char>(in.data(), in.length()));
        REQUIRE(wstatus == coro::poll_status::event);
        REQUIRE(wbytes == in.length());

        co_return;
    };

    coro::tcp_scheduler scheduler{coro::tcp_scheduler::options{
        .address       = coro::net::ip_address::from_string("0.0.0.0"),
        .port          = 8080,
        .backlog       = 128,
        .on_connection = on_connection,
        .io_options    = coro::io_scheduler::options{.thread_strategy = coro::io_scheduler::thread_strategy_t::spawn}}};

    auto make_client_task = [&scheduler, &msg]() -> coro::task<void> {
        coro::tcp_client client{
            scheduler,
            coro::tcp_client::options{.address = coro::net::ip_address::from_string("127.0.0.1"), .port = 8080, .domain = coro::net::domain_t::ipv4}};

        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::connect_status::connected);

        auto [wstatus, wbytes] =
            co_await scheduler.write(client.socket(), std::span<const char>{msg.data(), msg.length()});

        REQUIRE(wstatus == coro::poll_status::event);
        REQUIRE(wbytes == msg.length());

        std::string response(64, '\0');

        auto [rstatus, rbytes] =
            co_await scheduler.read(client.socket(), std::span<char>{response.data(), response.length()});

        REQUIRE(rstatus == coro::poll_status::event);
        REQUIRE(rbytes == msg.length());
        response.resize(rbytes);
        REQUIRE(response == msg);

        co_return;
    };

    scheduler.schedule(make_client_task());

    // Shutting down the scheduler will cause it to stop accepting new connections, to avoid requiring
    // another scheduler for this test the main thread can spin sleep until the tcp scheduler reports
    // that it is empty.  tcp schedulers do not report their accept task as a task in its size/empty count.
    while (!scheduler.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    scheduler.shutdown();
    REQUIRE(scheduler.empty());
}

TEST_CASE("tcp_scheduler echo server hostname")
{
    const std::string msg{"Hello from client with dns lookup"};

    auto on_connection = [&msg](coro::tcp_scheduler& scheduler, coro::net::socket sock) -> coro::task<void> {
        std::string in(64, '\0');

        auto [rstatus, rbytes] = co_await scheduler.read(sock, std::span<char>{in.data(), in.size()});
        REQUIRE(rstatus == coro::poll_status::event);

        in.resize(rbytes);
        REQUIRE(in == msg);

        auto [wstatus, wbytes] = co_await scheduler.write(sock, std::span<const char>(in.data(), in.length()));
        REQUIRE(wstatus == coro::poll_status::event);
        REQUIRE(wbytes == in.length());

        co_return;
    };

    coro::tcp_scheduler scheduler{coro::tcp_scheduler::options{
        .address       = coro::net::ip_address::from_string("0.0.0.0"),
        .port          = 8080,
        .backlog       = 128,
        .on_connection = on_connection,
        .io_options    = coro::io_scheduler::options{.thread_strategy = coro::io_scheduler::thread_strategy_t::spawn}}};

    coro::dns_client dns_client{scheduler, std::chrono::seconds{5}};

    auto make_client_task = [&scheduler, &dns_client, &msg]() -> coro::task<void> {
        coro::tcp_client client{
            scheduler,
            coro::tcp_client::options{
                .address = coro::net::hostname{"localhost"},
                .port = 8080,
                .domain = coro::net::domain_t::ipv4,
                .dns = &dns_client}};

        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::connect_status::connected);

        auto [wstatus, wbytes] =
            co_await scheduler.write(client.socket(), std::span<const char>{msg.data(), msg.length()});

        REQUIRE(wstatus == coro::poll_status::event);
        REQUIRE(wbytes == msg.length());

        std::string response(64, '\0');

        auto [rstatus, rbytes] =
            co_await scheduler.read(client.socket(), std::span<char>{response.data(), response.length()});

        REQUIRE(rstatus == coro::poll_status::event);
        REQUIRE(rbytes == msg.length());
        response.resize(rbytes);
        REQUIRE(response == msg);

        co_return;
    };

    scheduler.schedule(make_client_task());

    // Shutting down the scheduler will cause it to stop accepting new connections, to avoid requiring
    // another scheduler for this test the main thread can spin sleep until the tcp scheduler reports
    // that it is empty.  tcp schedulers do not report their accept task as a task in its size/empty count.
    while (!scheduler.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    scheduler.shutdown();
    REQUIRE(scheduler.empty());
}
