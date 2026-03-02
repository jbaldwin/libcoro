#pragma once
#include "coro/net/tcp/client.hpp"
#include "coro/task.hpp"
#include <vector>

namespace coro::ranges
{
template<class buffer_t>
class socket_stream
{
public:
    explicit socket_stream(coro::net::tcp::client& client, buffer_t&& buffer)
        : m_client(client),
          m_buffer{std::move(buffer)} {};

    auto advance() -> coro::task<bool>
    {
        auto [status, read] = co_await m_client.read_some(m_buffer);
        m_current_size      = read.size();

        if (not status.is_ok())
        {
            if (status.is_closed())
            {
                m_current_size = 0;
                co_return false;
            }
            throw std::runtime_error(status.message());
        }
        co_return true;
    }

    // Should be safe, because socket_stream gets moved
    // into further pipe objects
    auto get_value() const noexcept -> std::span<const std::byte> { return {m_buffer.data(), m_current_size}; }

private:
    coro::net::tcp::client& m_client;
    buffer_t                m_buffer;
    std::size_t             m_current_size{};
};

auto to_chunked_stream(coro::net::tcp::client& client, std::size_t buffer_size = 4096)
    -> socket_stream<std::vector<std::byte>>
{
    return socket_stream{client, std::vector<std::byte>{buffer_size}};
}

auto to_chunked_stream(coro::net::tcp::client& client, std::span<std::byte> external_buffer)
    -> socket_stream<std::span<std::byte>>
{
    return socket_stream{client, std::move(external_buffer)};
}
} // namespace coro::ranges