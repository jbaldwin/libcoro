#include "catch.hpp"

#include <coro/coro.hpp>

TEST_CASE("udp echo peers")
{
    const std::string client_msg{"Hello from client!"};
    const std::string server_msg{"Hello from server!!"};

    coro::io_scheduler scheduler{};

    auto make_client_task = [&](uint16_t client_port, uint16_t server_port) -> coro::task<void> {
        std::string owning_buffer(4096, '\0');

        coro::net::udp_client client{scheduler, coro::net::udp_client::options{.port = client_port}};
        auto wbytes = co_await client.sendto(std::span<const char>{client_msg.data(), client_msg.length()});
        REQUIRE(wbytes == client_msg.length());

        coro::net::udp_server server{scheduler, coro::net::udp_server::options{.port = server_port}};
        std::span<char> buffer{owning_buffer.data(), owning_buffer.length()};
        auto client_opt = co_await server.recvfrom(buffer);
        REQUIRE(client_opt.has_value());
        REQUIRE(buffer.size() == server_msg.length());
        owning_buffer.resize(buffer.size());
        REQUIRE(owning_buffer == server_msg);

        co_return;
    };

    auto make_server_task = [&](uint16_t server_port, uint16_t client_port) -> coro::task<void> {
        std::string owning_buffer(4096, '\0');

        coro::net::udp_server server{scheduler, coro::net::udp_server::options{.port = server_port}};

        std::span<char> buffer{owning_buffer.data(), owning_buffer.length()};
        auto client_opt = co_await server.recvfrom(buffer);
        REQUIRE(client_opt.has_value());
        REQUIRE(buffer.size() == client_msg.length());
        owning_buffer.resize(buffer.size());
        REQUIRE(owning_buffer == client_msg);


        auto options = client_opt.value();
        options.port = client_port; // we'll change the port for this test since its the same host
        coro::net::udp_client client{scheduler, options};
        auto wbytes = co_await client.sendto(std::span<const char>{server_msg.data(), server_msg.length()});
        REQUIRE(wbytes == server_msg.length());

        co_return;
    };

    scheduler.schedule(make_server_task(8080, 8081));
    scheduler.schedule(make_client_task(8080, 8081));
}
