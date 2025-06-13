//
// Created by pyxiion on 13.06.2025.
//
#include "coro/detail/signal_unix.hpp"
#include <unistd.h>

namespace coro::detail
{
signal_unix::signal_unix()
{
    ::pipe(m_pipe.data());
}
signal_unix::~signal_unix()
{
    for (auto& fd : m_pipe)
    {
        if (fd != -1)
        {
            close(fd);
            fd = -1;
        }
    }
}
void signal_unix::set()
{
    const int value{1};
    ::write(m_pipe[1], reinterpret_cast<const void*>(&value), sizeof(value));
}
void signal_unix::unset()
{
    int control = 0;
    ::read(m_pipe[1], reinterpret_cast<void*>(&control), sizeof(control));
}
} // namespace coro::detail