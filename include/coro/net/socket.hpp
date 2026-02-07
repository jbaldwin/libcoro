#pragma once

#include "coro/net/socket_address.hpp"
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
        /// udp datagram socket
        udp,
        /// tcp streaming socket
        tcp
    };

    enum class blocking_t
    {
        /// This socket should block on system calls.
        yes,
        /// This socket should not block on system calls.
        no
    };

    struct options
    {
        /// The type of socket.
        type_t type;
        /// If the socket should be blocking or non-blocking.
        blocking_t blocking;
    };

    static auto type_to_os(type_t type) -> int;

    socket() = default;
    explicit socket(int fd) : m_fd(fd) {}

    socket(const socket& other) : m_fd(dup(other.m_fd)) {}
    socket(socket&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {}
    auto operator=(const socket& other) noexcept -> socket&;
    auto operator=(socket&& other) noexcept -> socket&;

    ~socket() { close(); }

    /**
     * This function returns true if the socket's file descriptor is a valid number, however it does
     * not imply if the socket is still usable.
     * @return True if the socket file descriptor is > 0.
     */
    [[nodiscard]] auto is_ok() const -> bool { return m_fd != -1; }

    explicit operator bool() const { return is_ok(); }

    /**
     * @param block Sets the socket to the given blocking mode.
     */
    auto blocking(blocking_t block) -> bool;

    /**
     * @param how Shuts the socket down with the given operations.
     * @return Returns true if the sockets given operations were shutdown.
     */
    auto shutdown(poll_op how = poll_op::read_write) -> bool;

    /**
     * Closes the socket and sets this socket to an invalid state.
     */
    auto close() -> void;

    /**
     * @return The native handle (file descriptor) for this socket.
     */
    auto native_handle() const -> int { return m_fd; }

private:
    int m_fd{-1};
};

/**
 * Creates a socket with the given socket options, this typically is used for creating sockets to
 * use within client objects, e.g. tcp::client and udp::client.
 * @param opts See socket::options for more details.
 * TODO: docs
 */
auto make_socket(const socket::options& opts, domain_t) -> socket;

/**
 * Creates a socket that can accept connections or packets with the given socket options, address,
 * port and backlog.  This is used for creating sockets to use within server objects, e.g.
 * tcp::server and udp::server.
 * @param opts See socket::options for more details
 * @param address The ip address to bind to.  If the type of socket is tcp then it will also listen.
 * @param port The port to bind to.
 * @param backlog If the type of socket is tcp then the backlog of connections to allow.  Does nothing
 *                for udp types.
 * TODO: docs
 */
auto make_accept_socket(const socket::options& opts, const net::socket_address& endpoint, int32_t backlog) -> socket;

} // namespace coro::net
