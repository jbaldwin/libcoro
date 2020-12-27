#pragma once

#include "coro/connect.hpp"
#include "coro/poll.hpp"
#include "coro/socket.hpp"
#include "coro/task.hpp"

#include <chrono>

namespace coro
{
class io_scheduler;

class tcp_client
{
public:
    struct options
    {
        std::string      address{"127.0.0.1"};
        int16_t          port{8080};
        socket::domain_t domain{socket::domain_t::ipv4};
    };

    tcp_client(io_scheduler& scheduler, options opts = options{"127.0.0.1", 8080, socket::domain_t::ipv4});
    tcp_client(const tcp_client&) = delete;
    tcp_client(tcp_client&&)      = default;
    auto operator=(const tcp_client&) noexcept -> tcp_client& = delete;
    auto operator=(tcp_client&&) noexcept -> tcp_client& = default;
    ~tcp_client()                                        = default;

    auto connect(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<connect_status>;

    auto socket() const -> const coro::socket& { return m_socket; }
    auto socket() -> coro::socket& { return m_socket; }

private:
    io_scheduler& m_io_scheduler;
    options       m_options;
    coro::socket  m_socket;
};

} // namespace coro
