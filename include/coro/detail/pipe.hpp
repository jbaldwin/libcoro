#include "coro/fd.hpp"

#include <array>

namespace coro::detail
{

class pipe_t
{
public:
    explicit pipe_t();
    ~pipe_t();

    pipe_t(const pipe_t& other);
    pipe_t(pipe_t&& other) noexcept;

    auto operator=(const pipe_t& other) -> pipe_t&;
    auto operator=(pipe_t&& other) noexcept -> pipe_t&;

    [[nodiscard]] auto read_fd() const -> const fd_t&;
    [[nodiscard]] auto write_fd() const -> const fd_t&;

    auto close() -> void;

private:
    std::array<fd_t, 2> m_fds{-1};
};


} // namespace coro::detail
