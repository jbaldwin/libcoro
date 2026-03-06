#pragma once
#include "coro/net/tcp/client.hpp"
#include "coro/task.hpp"
#include <vector>

namespace coro::ranges
{
template<class client_t, class buffer_t>
class socket_stream
{
public:
    explicit socket_stream(client_t client, buffer_t buffer)
        : m_client(std::forward<client_t>(client)),
          m_buffer{std::move(buffer)} {};

    socket_stream(socket_stream&)            = delete;
    socket_stream& operator=(socket_stream&) = delete;

    socket_stream(socket_stream&& other) noexcept
        : m_client(std::move(other.m_client)),
          m_buffer(std::move(other.m_buffer))
    {
    }
    socket_stream& operator=(socket_stream&& other) noexcept
    {
        if (std::addressof(other) != this)
        {
            m_client = std::move(other.m_client);
            m_buffer = std::move(other.m_buffer);
        }
        return *this;
    }

    // Should be safe, because socket_stream gets moved
    // into further pipe objects
    auto next() -> coro::task<std::optional<std::span<const std::byte>>>
    {
        auto [status, read] = co_await m_client.read_some(m_buffer);
        m_current_size      = read.size();

        if (not status.is_ok())
        {
            if (status.is_closed())
            {
                m_current_size = 0;
                co_return std::nullopt;
            }
            throw std::runtime_error(status.message());
        }
        co_return std::span{m_buffer}.subspan(0, m_current_size);
    }

private:
    client_t    m_client;
    buffer_t    m_buffer;
    std::size_t m_current_size{};
};

struct _with_buffer
{
    template<class client_t>
        requires std::is_same_v<std::remove_cvref_t<client_t>, coro::net::tcp::client>
    constexpr auto operator()(client_t&& client, std::size_t buffer_size = 4096) const
    {
        return socket_stream{std::forward<client_t>(client), std::vector<std::byte>{buffer_size}};
    }

    constexpr auto operator()(std::size_t buffer_size = 4096) const
    {
        return _partial<_with_buffer, std::size_t>{0, buffer_size};
    }
};

inline constexpr _with_buffer with_buffer;

template<class client_t>
constexpr auto operator|(client_t&& client, _partial<_with_buffer, std::size_t>&& with_buf)
{
    return with_buf(client);
}
} // namespace coro::ranges