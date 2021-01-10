#include "catch.hpp"

#include <coro/coro.hpp>

TEST_CASE("tcp_server ping server")
{
    const std::string client_msg{"Hello from client"};
    const std::string server_msg{"Reply from server!"};

    coro::io_scheduler scheduler{};

    auto make_client_task = [&]() -> coro::task<void> {
        coro::net::tcp_client client{scheduler};

        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::net::connect_status::connected);

        // Skip polling for write, should really only poll if the write is partial, shouldn't be
        // required for this test.
        auto [sstatus, remaining] = client.send(client_msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());

        // Poll for the server's response.
        auto pstatus = co_await client.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::event);

        std::string buffer(256, '\0');
        auto [rstatus, rspan] = client.recv(buffer);
        REQUIRE(rstatus == coro::net::recv_status::ok);
        REQUIRE(rspan.size() == server_msg.length());
        buffer.resize(rspan.size());
        REQUIRE(buffer == server_msg);

        co_return;
    };

    auto make_server_task = [&]() -> coro::task<void> {
        coro::net::tcp_server server{scheduler};

        // Poll for client connection.
        auto pstatus = co_await server.poll();
        REQUIRE(pstatus == coro::poll_status::event);
        auto client = server.accept();
        REQUIRE(client.socket().is_valid());

        // Poll for client request.
        pstatus = co_await client.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::event);

        std::string buffer(256, '\0');
        auto [rstatus, rspan] = client.recv(buffer);
        REQUIRE(rstatus == coro::net::recv_status::ok);
        REQUIRE(rspan.size() == client_msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == client_msg);

        // Respond to client.
        auto [sstatus, remaining] = client.send(server_msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());
    };

    scheduler.schedule(make_server_task());
    scheduler.schedule(make_client_task());

    while(!scheduler.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}
