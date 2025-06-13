#pragma once
#include "coro/fd.hpp"

#include <array>

namespace coro::detail
{
class signal_unix
{
public:
    signal_unix();
    ~signal_unix();

    void set();

    void unset();

    [[nodiscard]] fd_t read_fd() const noexcept { return m_pipe[0]; }
    [[nodiscard]] fd_t write_fd() const noexcept { return m_pipe[1]; }

private:
    std::array<fd_t, 2> m_pipe{-1};
};
} // namespace coro::detail