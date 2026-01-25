#include "catch_amalgamated.hpp"
#include "catch_extensions.hpp"
#include "coro/net/socket_address.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING

    #include <coro/coro.hpp>

    #include <iostream>

TEST_CASE("tcp_server", "[tcp_server]")
{
    std::cerr << "[tcp_server]\n\n";
}

TEST_CASE("tcp_server ping server", "[tcp_server]")
{
    std::cerr << "BEGIN tcp_server ping server\n";
    const std::string client_msg{"Hello from client"};
    const std::string server_msg{"Reply from server!"};

    const auto endpoint = GENERATE(
        as<coro::net::socket_address>{},
        coro::net::socket_address{"127.0.0.1", 8080},
        coro::net::socket_address{"::1", 8080, coro::net::domain_t::ipv6});

    auto scheduler = coro::scheduler::make_unique(
        coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_client_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                               const std::string&                   client_msg,
                               const std::string&                   server_msg,
                               const coro::net::socket_address&           endpoint) -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::tcp::client client{scheduler, endpoint};

        std::cerr << "client connect\n";
        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::net::connect_status::connected);

        std::cerr << "client write_all()\n";
        auto [sstatus, remaining] = co_await client.write_all(client_msg);
        REQUIRE(sstatus.is_ok());
        REQUIRE(remaining.empty());

        std::string buffer(256, '\0');
        std::cerr << "client read_some()\n";
        auto [rstatus, rspan] = co_await client.read_some(buffer);
        REQUIRE(rstatus.is_ok());
        REQUIRE(rspan.size() == server_msg.length());
        buffer.resize(rspan.size());
        REQUIRE(buffer == server_msg);

        std::cerr << "client return\n";
        co_return;
    };

    auto make_server_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                               const std::string&                   client_msg,
                               const std::string&                   server_msg,
                               const coro::net::socket_address&           endpoint) -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler, endpoint};

        std::cerr << "server accept()\n";
        auto client = co_await server.accept();
        REQUIRE(client.has_value());
        REQUIRE(client->socket().is_valid());

        std::string buffer(256, '\0');
        std::cerr << "server read_some()\n";
        auto [rstatus, rspan] = co_await client->read_some(buffer);
        REQUIRE(rstatus.is_ok());
        REQUIRE(rspan.size() == client_msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == client_msg);

        // Respond to client.
        std::cerr << "server send()\n";
        auto [wstatus, remaining] = co_await client->write_some(server_msg);
        REQUIRE(wstatus.is_ok());
        REQUIRE(remaining.empty());

        std::cerr << "server return\n";
        co_return;
    };

    DYNAMIC_SECTION("Testing with domain: " << (endpoint.domain() == coro::net::domain_t::ipv4 ? "ipv4" : "ipv6")) {
        coro::sync_wait(
            coro::when_all(
                make_server_task(scheduler, client_msg, server_msg, endpoint),
                make_client_task(scheduler, client_msg, server_msg, endpoint)));
    }
    std::cerr << "END tcp_server ping server\n";
}

TEST_CASE("tcp_server concurrent polling on the same socket", "[tcp_server]")
{
    std::cerr << "BEGIN tcp_server concurrent polling on the same socket\n";
    // Issue 224: This test duplicates a client and issues two different poll operations per coroutine.

    using namespace std::chrono_literals;
    auto scheduler = coro::scheduler::make_unique(
        coro::scheduler::options{
            .execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_inline});
    const auto endpoint = coro::net::socket_address{"127.0.0.1", 8080};

    auto make_server_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                               const coro::net::socket_address&           endpoint) -> coro::task<std::string>
    {
        auto make_read_task = [](coro::net::tcp::client client) -> coro::task<void>
        {
            co_await client.poll(coro::poll_op::read, 2s);
            co_return;
        };

        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler, endpoint};

        auto read_client = co_await server.accept();
        REQUIRE(read_client.has_value());
        REQUIRE(read_client->socket().is_valid());

        // make a copy so we can poll twice at the same time in different coroutines
        auto write_client = *read_client;

        scheduler->spawn_detached(make_read_task(std::move(*read_client)));

        // Make sure the read op has completed.
        co_await scheduler->yield_for(500ms);

        std::string data(8096, 'A');
        auto [send_status, r] = co_await write_client.write_all(data);
        REQUIRE(send_status.is_ok());

        co_return data;
    };

    auto make_client_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                               const coro::net::socket_address&           endpoint) -> coro::task<std::string>
    {
        co_await scheduler->schedule();
        coro::net::tcp::client client{scheduler, endpoint};

        auto connect_status = co_await client.connect();
        REQUIRE(connect_status == coro::net::connect_status::connected);

        std::string     response(8096, '\0');
        std::span<char> remaining{response};
        do
        {
            auto pstatus = co_await client.poll(coro::poll_op::read);
            if (pstatus != coro::poll_status::read)
            {
                REQUIRE_THREAD_SAFE(pstatus == coro::poll_status::closed);
                // the socket has been closed
                co_return response;
            }
            REQUIRE(pstatus == coro::poll_status::read);

            auto [recv_status, r] = client.recv(remaining);
            remaining             = remaining.subspan(r.size_bytes());

        } while (!remaining.empty());

        co_return response;
    };

    auto result =
        coro::sync_wait(coro::when_all(make_server_task(scheduler, endpoint), make_client_task(scheduler, endpoint)));

    auto request  = std::move(std::get<0>(result).return_value());
    auto response = std::move(std::get<1>(result).return_value());

    REQUIRE(request == response);
    std::cerr << "END tcp_server concurrent polling on the same socket\n";
}

// This test is known to not work on kqueue style systems (e.g. apple) because the socket shutdown()
// call does not properly trigger an EV_EOF flag on the accept socket.

TEST_CASE("tcp_server graceful shutdown via socket", "[tcp_server]")
{
    std::cerr << "BEGIN tcp_server graceful shutdown via socket\n";
    auto scheduler = coro::scheduler::make_unique(
        coro::scheduler::options{
            .execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_inline});
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
