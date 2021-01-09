#pragma once

#include "coro/net/ip_address.hpp"
#include "coro/poll.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <span>
#include <unistd.h>
#include <utility>

#include <iostream>

namespace coro::net
{

class socket
{
public:
    enum class type_t
    {
        udp,
        tcp
    };

    enum class blocking_t
    {
        yes,
        no
    };

    struct options
    {
        domain_t   domain;
        type_t     type;
        blocking_t blocking;
    };

    static auto type_to_os(type_t type) -> int;

    socket() = default;

    explicit socket(int fd) : m_fd(fd) {}

    socket(const socket&) = delete;
    socket(socket&& other) : m_fd(std::exchange(other.m_fd, -1)) {}

    auto operator=(const socket&) -> socket& = delete;

    auto operator=(socket&& other) noexcept -> socket&;

    ~socket() { close(); }

    auto blocking(blocking_t block) -> bool;

    auto shutdown(poll_op how = poll_op::read_write) -> bool;

    auto close() -> void;

    auto native_handle() const -> int { return m_fd; }

private:
    int m_fd{-1};
};

auto make_socket(const socket::options& opts) -> socket;

auto make_accept_socket(
    const socket::options&         opts,
    const net::ip_address& address,
    uint16_t               port,
    int32_t                backlog = 128) -> socket;

} // namespace coro::net
