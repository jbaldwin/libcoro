#include "catch_amalgamated.hpp"
#include "catch_extensions.hpp"

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

    auto scheduler = coro::io_scheduler::make_unique(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_client_task = [](std::unique_ptr<coro::io_scheduler>& scheduler,
                               const std::string&                   client_msg,
                               const std::string&                   server_msg) -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::tcp::client client{scheduler};

        std::cerr << "client connect\n";
        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::net::connect_status::connected);

        // Skip polling for write, should really only poll if the write is partial, shouldn't be
        // required for this test.
        std::cerr << "client send()\n";
        auto [sstatus, remaining] = client.send(client_msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());

        // Poll for the server's response.
        std::cerr << "client poll(read)\n";
        auto pstatus = co_await client.poll(coro::poll_op::read);
        if (pstatus != coro::poll_status::read)
        {
            REQUIRE_THREAD_SAFE(pstatus == coro::poll_status::closed);
            // the socket has been closed
            co_return;
        }
        REQUIRE(pstatus == coro::poll_status::read);

        std::string buffer(256, '\0');
        std::cerr << "client recv()\n";
        auto [rstatus, rspan] = client.recv(buffer);
        REQUIRE(rstatus == coro::net::recv_status::ok);
        REQUIRE(rspan.size() == server_msg.length());
        buffer.resize(rspan.size());
        REQUIRE(buffer == server_msg);

        std::cerr << "client return\n";
        co_return;
    };

    auto make_server_task = [](std::unique_ptr<coro::io_scheduler>& scheduler,
                               const std::string&                   client_msg,
                               const std::string&                   server_msg) -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler};

        // Poll for client connection.
        std::cerr << "server poll(accept)\n";
        auto pstatus = co_await server.poll();
        REQUIRE(pstatus == coro::poll_status::read);
        std::cerr << "server accept()\n";
        auto client = server.accept();
        REQUIRE(client.socket().is_valid());

        // Poll for client request.
        std::cerr << "server poll(read)\n";
        pstatus = co_await client.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::read);

        std::string buffer(256, '\0');
        std::cerr << "server recv()\n";
        auto [rstatus, rspan] = client.recv(buffer);
        REQUIRE(rstatus == coro::net::recv_status::ok);
        REQUIRE(rspan.size() == client_msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == client_msg);

        // Respond to client.
        std::cerr << "server send()\n";
        auto [sstatus, remaining] = client.send(server_msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());

        std::cerr << "server return\n";
        co_return;
    };

    coro::sync_wait(
        coro::when_all(
            make_server_task(scheduler, client_msg, server_msg), make_client_task(scheduler, client_msg, server_msg)));
    std::cerr << "END tcp_server ping server\n";
}

TEST_CASE("tcp_server concurrent polling on the same socket", "[tcp_server]")
{
    std::cerr << "BEGIN tcp_server concurrent polling on the same socket\n";
    // Issue 224: This test duplicates a client and issues two different poll operations per coroutine.

    using namespace std::chrono_literals;
    auto scheduler = coro::io_scheduler::make_unique(
        coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});

    auto make_server_task = [](std::unique_ptr<coro::io_scheduler>& scheduler) -> coro::task<std::string>
    {
        auto make_read_task = [](coro::net::tcp::client client) -> coro::task<void>
        {
            co_await client.poll(coro::poll_op::read, 2s);
            co_return;
        };

        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler};

        auto poll_status = co_await server.poll();
        REQUIRE(poll_status == coro::poll_status::read);

        auto read_client = server.accept();
        REQUIRE(read_client.socket().is_valid());

        // make a copy so we can poll twice at the same time in different coroutines
        auto write_client = read_client;

        scheduler->spawn(make_read_task(std::move(read_client)));

        // Make sure the read op has completed.
        co_await scheduler->yield_for(500ms);

        std::string           data(8096, 'A');
        std::span<const char> remaining{data};
        do
        {
            auto poll_status = co_await write_client.poll(coro::poll_op::write);
            REQUIRE(poll_status == coro::poll_status::write);
            auto [send_status, r] = write_client.send(remaining);
            REQUIRE(send_status == coro::net::send_status::ok);

            if (r.empty())
            {
                break;
            }

            remaining = r;
        } while (true);

        co_return data;
    };

    auto make_client_task = [](std::unique_ptr<coro::io_scheduler>& scheduler) -> coro::task<std::string>
    {
        co_await scheduler->schedule();
        coro::net::tcp::client client{scheduler};

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

    auto result = coro::sync_wait(coro::when_all(make_server_task(scheduler), make_client_task(scheduler)));

    auto request  = std::move(std::get<0>(result).return_value());
    auto response = std::move(std::get<1>(result).return_value());

    REQUIRE(request == response);
    std::cerr << "END tcp_server concurrent polling on the same socket\n";
}

    #ifndef __APPLE__
// This test is known to not work on kqueue style systems (e.g. apple) because the socket shutdown()
// call does not properly trigger an EV_EOF flag on the accept socket.

TEST_CASE("tcp_server graceful shutdown via socket", "[tcp_server]")
{
    std::cerr << "BEGIN tcp_server graceful shutdown via socket\n";
    auto scheduler = coro::io_scheduler::make_unique(
        coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
    coro::net::tcp::server server{scheduler};
    coro::event            started{};

    auto make_accept_task = [](coro::net::tcp::server& server, coro::event& started) -> coro::task<void>
    {
        std::cerr << "make accept task start\n";
        started.set();
        auto poll_result = co_await server.poll();
        REQUIRE(poll_result == coro::poll_status::closed);
        auto client = server.accept();
        REQUIRE_FALSE(client.socket().is_valid());
        std::cerr << "make accept task completed\n";
    };

    scheduler->spawn(make_accept_task(server, started));

    coro::sync_wait(started);
    // we'll wait a bit to make sure the server.poll() is fully registered.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    server.accept_socket().shutdown(coro::poll_op::read_write);

    scheduler->shutdown();
    std::cerr << "END tcp_server graceful shutdown via socket\n";
}

    #endif

TEST_CASE("~tcp_server", "[tcp_server]")
{
    std::cerr << "[~tcp_server]\n\n";
}

#endif // LIBCORO_FEATURE_NETWORKING
