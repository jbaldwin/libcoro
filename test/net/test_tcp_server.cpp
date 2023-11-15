#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("tcp_server ping server", "[tcp_server]")
{
    const std::string client_msg{"Hello from client"};
    const std::string server_msg{"Reply from server!"};

    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_client_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::tcp_client client{scheduler};

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
        REQUIRE(pstatus == coro::poll_status::event);

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

    auto make_server_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::tcp_server server{scheduler};

        // Poll for client connection.
        std::cerr << "server poll(accept)\n";
        auto pstatus = co_await server.poll();
        REQUIRE(pstatus == coro::poll_status::event);
        std::cerr << "server accept()\n";
        auto client = server.accept();
        REQUIRE(client.socket().is_valid());

        // Poll for client request.
        std::cerr << "server poll(read)\n";
        pstatus = co_await client.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::event);

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

    coro::sync_wait(coro::when_all(make_server_task(), make_client_task()));
}

TEST_CASE("tcp_server with ssl", "[tcp_server]")
{
    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    std::string client_msg = "Hello world from SSL client!";
    std::string server_msg = "Hello world from SSL server!!";

    auto make_client_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tcp_client client{
            scheduler, coro::net::tcp_client::options{.ssl_ctx = std::make_shared<coro::net::ssl_context>()}};

        std::cerr << "client.connect()\n";
        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::net::connect_status::connected);
        std::cerr << "client.connected\n";

        std::cerr << "client.ssl_handshake()\n";
        auto hstatus = co_await client.ssl_handshake();
        REQUIRE(hstatus == coro::net::ssl_handshake_status::ok);

        std::cerr << "client.poll(write)\n";
        auto pstatus = co_await client.poll(coro::poll_op::write);
        REQUIRE(pstatus == coro::poll_status::event);

        std::cerr << "client.send()\n";
        auto [sstatus, remaining] = client.send(client_msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());

        std::string response;
        response.resize(256, '\0');

        while (true)
        {
            std::cerr << "client.poll(read)\n";
            pstatus = co_await client.poll(coro::poll_op::read);
            REQUIRE(pstatus == coro::poll_status::event);

            std::cerr << "client.recv()\n";
            auto [rstatus, rspan] = client.recv(response);
            if (rstatus == coro::net::recv_status::would_block)
            {
                std::cerr << coro::net::to_string(rstatus) << "\n";
                continue;
            }
            else
            {
                std::cerr << coro::net::to_string(rstatus) << "\n";
                REQUIRE(rstatus == coro::net::recv_status::ok);
                REQUIRE(rspan.size() == server_msg.size());
                response.resize(rspan.size());
                break;
            }
        }

        REQUIRE(response == server_msg);
        std::cerr << "client received message: " << response << "\n";

        std::cerr << "client finished\n";
        co_return;
    };

    auto make_server_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tcp_server server{
            scheduler,
            coro::net::tcp_server::options{
                .ssl_ctx = std::make_shared<coro::net::ssl_context>(
                    "cert.pem", coro::net::ssl_file_type::pem, "key.pem", coro::net::ssl_file_type::pem)}};

        std::cerr << "server.poll()\n";
        auto pstatus = co_await server.poll();
        REQUIRE(pstatus == coro::poll_status::event);

        std::cerr << "server.accept()\n";
        auto client = server.accept();
        REQUIRE(client.socket().is_valid());

        std::cerr << "server client.handshake()\n";
        auto hstatus = co_await client.ssl_handshake();
        REQUIRE(hstatus == coro::net::ssl_handshake_status::ok);

        std::cerr << "server client.poll(read)\n";
        pstatus = co_await client.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::event);

        std::string buffer;
        buffer.resize(256, '\0');
        std::cerr << "server client.recv()\n";
        auto [rstatus, rspan] = client.recv(buffer);
        REQUIRE(rstatus == coro::net::recv_status::ok);
        REQUIRE(rspan.size() == client_msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == client_msg);
        std::cerr << "server received message: " << buffer << "\n";

        std::cerr << "server client.poll(write)\n";
        pstatus = co_await client.poll(coro::poll_op::write);
        REQUIRE(pstatus == coro::poll_status::event);

        std::cerr << "server client.send()\n";
        auto [sstatus, remaining] = client.send(server_msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());

        std::cerr << "server finished\n";
        co_return;
    };

    coro::sync_wait(coro::when_all(make_server_task(), make_client_task()));
}