#include "coro/detail/pipe.hpp"

#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <cerrno>

namespace coro::detail
{

pipe_t::pipe_t()
{
    // Using pipe instead of pipe2 since macos does not have support for pipe2.
    if (::pipe(m_fds.data()) != 0)
    {
        const std::string msg = "Failed to create pipe, errno=[" + std::string{std::strerror(errno)} + "]";
        throw std::runtime_error(msg);
    }

    // Set the pipe file descriptors to be non-blocking.
    for (const auto& fd : m_fds)
    {
        int flags = fcntl(fd, F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(fd, F_SETFL, flags);
    }
}

pipe_t::~pipe_t()
{
    close();
}

pipe_t::pipe_t(const pipe_t& other)
{
    m_fds[0] = dup(other.m_fds[0]);
    m_fds[1] = dup(other.m_fds[1]);
}

pipe_t::pipe_t(pipe_t&& other) noexcept
{
    m_fds = std::exchange(other.m_fds, {-1});
}

auto pipe_t::operator=(const pipe_t& other) -> pipe_t&
{
    if (std::addressof(other) != this)
    {
        m_fds[0] = dup(other.m_fds[0]);
        m_fds[1] = dup(other.m_fds[1]);
    }

    return *this;
}

auto pipe_t::operator=(pipe_t&& other) noexcept -> pipe_t&
{
    if (std::addressof(other) != this)
    {
        m_fds = std::exchange(other.m_fds, {-1});
    }

    return *this;
}

auto pipe_t::read_fd() const -> const fd_t&
{
    return m_fds[0];
}

auto pipe_t::write_fd() const -> const fd_t&
{
    return m_fds[1];
}

auto pipe_t::close() -> void
{
    if (m_fds[0] != -1)
    {
        ::close(m_fds[0]);
        m_fds[0] = -1;
    }

    if (m_fds[1] != -1)
    {
        ::close(m_fds[1]);
        m_fds[1] = -1;
    }
}

} // namespace coro::detail
