#include "coro/net/tcp/client.hpp"

namespace coro::net::tcp
{
using namespace std::chrono_literals;

client::client(std::shared_ptr<io_scheduler> scheduler, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_socket(net::make_socket(
          net::socket::options{m_options.address.domain(), net::socket::type_t::tcp, net::socket::blocking_t::no}))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp::client cannot have nullptr io_scheduler"};
    }
}

client::client(std::shared_ptr<io_scheduler> scheduler, net::socket socket, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_socket(std::move(socket)),
      m_connect_status(connect_status::connected)
{
    // io_scheduler is assumed good since it comes from a tcp::server.

    // Force the socket to be non-blocking.
    m_socket.blocking(coro::net::socket::blocking_t::no);
}

client::client(const client& other)
    : m_io_scheduler(other.m_io_scheduler),
      m_options(other.m_options),
      m_socket(other.m_socket),
      m_connect_status(other.m_connect_status)
{
}

client::client(client&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_options(std::move(other.m_options)),
      m_socket(std::move(other.m_socket)),
      m_connect_status(std::exchange(other.m_connect_status, std::nullopt))
{
}

client::~client()
{
}

auto client::operator=(const client& other) noexcept -> client&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler   = other.m_io_scheduler;
        m_options        = other.m_options;
        m_socket         = other.m_socket;
        m_connect_status = other.m_connect_status;
    }
    return *this;
}

auto client::operator=(client&& other) noexcept -> client&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler   = std::move(other.m_io_scheduler);
        m_options        = std::move(other.m_options);
        m_socket         = std::move(other.m_socket);
        m_connect_status = std::exchange(other.m_connect_status, std::nullopt);
    }
    return *this;
}

auto client::connect(std::chrono::milliseconds timeout) -> coro::task<connect_status>
{
    // Only allow the user to connect per tcp client once, if they need to re-connect they should
    // make a new tcp::client.
    if (m_connect_status.has_value())
    {
        co_return m_connect_status.value();
    }

    // This enforces the connection status is aways set on the client object upon returning.
    auto return_value = [this](connect_status s) -> connect_status
    {
        m_connect_status = s;
        return s;
    };

    sockaddr_in server{};
    server.sin_family = static_cast<int>(m_options.address.domain());
    server.sin_port   = htons(m_options.port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(m_options.address.data().data());

    auto cret = ::connect(m_socket.native_handle(), reinterpret_cast<struct sockaddr*>(&server), sizeof(server));
    if (cret == 0)
    {
        co_return return_value(connect_status::connected);
    }
    else if (cret == -1)
    {
        // If the connect is happening in the background poll for write on the socket to trigger
        // when the connection is established.
        if (errno == EAGAIN || errno == EINPROGRESS)
        {
            auto pstatus = co_await m_io_scheduler->poll(m_socket, poll_op::write, timeout);
            if (pstatus == poll_status::event)
            {
                int       result{0};
                socklen_t result_length{sizeof(result)};
                if (getsockopt(m_socket.native_handle(), SOL_SOCKET, SO_ERROR, &result, &result_length) < 0)
                {
                    std::cerr << "connect failed to getsockopt after write poll event\n";
                }

                if (result == 0)
                {
                    co_return return_value(connect_status::connected);
                }
            }
            else if (pstatus == poll_status::timeout)
            {
                co_return return_value(connect_status::timeout);
            }
        }
    }

    co_return return_value(connect_status::error);
}

} // namespace coro::net::tcp
