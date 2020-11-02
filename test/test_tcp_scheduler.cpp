#include "catch.hpp"

#include <coro/coro.hpp>

TEST_CASE("tcp_scheduler no on connection throws")
{
    REQUIRE_THROWS(coro::tcp_scheduler{coro::tcp_scheduler::options{.on_connection = nullptr}});
}

TEST_CASE("tcp_scheduler ping")
{
    std::string msg{"Hello from client"};

    auto on_connection = [&](coro::tcp_scheduler& tcp, coro::socket sock) -> coro::task<void> {
        /*auto status =*/co_await tcp.poll(sock.native_handle(), coro::poll_op::read);
        /*REQUIRE(status == coro::poll_status::success);*/

        std::string in{};
        in.resize(2048, '\0');
        auto read_bytes = sock.recv(std::span<char>{in.data(), in.size()});
        REQUIRE(read_bytes == msg.length());
        in.resize(read_bytes);
        REQUIRE(in == msg);

        /*status =*/co_await tcp.poll(sock.native_handle(), coro::poll_op::write);
        /*REQUIRE(status == coro::poll_status::success);*/

        auto written_bytes = sock.send(std::span<const char>(in.data(), in.length()));
        REQUIRE(written_bytes == in.length());

        co_return;
    };

    coro::tcp_scheduler tcp{coro::tcp_scheduler::options{
        .address       = "0.0.0.0",
        .port          = 8080,
        .backlog       = 128,
        .on_connection = on_connection,
        .io_options    = coro::io_scheduler::options{8, 2, coro::io_scheduler::thread_strategy_t::spawn}}};

    int         client_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port   = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &server.sin_addr) <= 0)
    {
        perror("failed to set sin_addr=127.0.0.1");
        REQUIRE(false);
    }

    if (connect(client_socket, (struct sockaddr*)&server, sizeof(server)) < 0)
    {
        perror("Failed to connect to tcp scheduler server");
        REQUIRE(false);
    }
    ::send(client_socket, msg.data(), msg.length(), 0);

    std::string response{};
    response.resize(256, '\0');
    auto bytes_recv = ::recv(client_socket, response.data(), response.length(), 0);
    REQUIRE(bytes_recv == msg.length());
    response.resize(bytes_recv);
    REQUIRE(response == msg);

    tcp.shutdown();
    REQUIRE(tcp.empty());

    close(client_socket);
}
