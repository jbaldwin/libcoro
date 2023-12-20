#include "catch_amalgamated.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING
    #ifdef LIBCORO_FEATURE_TLS

        #include <coro/coro.hpp>

        #include <iostream>

TEST_CASE("tls_server hello world server", "[tls_server]")
{
    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    std::string client_msg = "Hello world from TLS client!";
    std::string server_msg = "Hello world from TLS server!!";

    auto make_client_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tls::client client{
            scheduler, std::make_shared<coro::net::tls::context>(coro::net::tls::verify_peer_t::no)};

        std::cerr << "client.connect()\n";
        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::net::tls::connection_status::connected);
        std::cerr << "client.connected\n";

        std::cerr << "client.poll(write)\n";
        auto pstatus = co_await client.poll(coro::poll_op::write);
        REQUIRE(pstatus == coro::poll_status::event);

        std::cerr << "client.send()\n";
        auto [sstatus, remaining] = client.send(client_msg);
        REQUIRE(sstatus == coro::net::tls::send_status::ok);
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
            if (rstatus == coro::net::tls::recv_status::want_read)
            {
                std::cerr << coro::net::tls::to_string(rstatus) << "\n";
                continue;
            }
            else
            {
                std::cerr << coro::net::tls::to_string(rstatus) << "\n";
                REQUIRE(rstatus == coro::net::tls::recv_status::ok);
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

        coro::net::tls::server server{
            scheduler,
            std::make_shared<coro::net::tls::context>(
                "cert.pem", coro::net::tls::tls_file_type::pem, "key.pem", coro::net::tls::tls_file_type::pem)};

        std::cerr << "server.poll()\n";
        auto pstatus = co_await server.poll();
        REQUIRE(pstatus == coro::poll_status::event);

        std::cerr << "server.accept()\n";
        auto client = co_await server.accept();
        REQUIRE(client.socket().is_valid());

        std::cerr << "server client.poll(read)\n";
        pstatus = co_await client.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::event);

        std::string buffer;
        buffer.resize(256, '\0');
        std::cerr << "server client.recv()\n";
        auto [rstatus, rspan] = client.recv(buffer);
        REQUIRE(rstatus == coro::net::tls::recv_status::ok);
        REQUIRE(rspan.size() == client_msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == client_msg);
        std::cerr << "server received message: " << buffer << "\n";

        std::cerr << "server client.poll(write)\n";
        pstatus = co_await client.poll(coro::poll_op::write);
        REQUIRE(pstatus == coro::poll_status::event);

        std::cerr << "server client.send()\n";
        auto [sstatus, remaining] = client.send(server_msg);
        REQUIRE(sstatus == coro::net::tls::send_status::ok);
        REQUIRE(remaining.empty());

        std::cerr << "server finished\n";
        co_return;
    };

    coro::sync_wait(coro::when_all(make_server_task(), make_client_task()));
}

    #endif // LIBCORO_FEATURE_TLS
#endif     // LIBCORO_FEATURE_NETWORKING
