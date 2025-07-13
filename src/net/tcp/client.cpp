#include "coro/net/tcp/client.hpp"

#if defined(CORO_PLATFORM_WINDOWS)
// The order of includes matters
// clang-format off
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <coro/detail/iocp_overlapped.hpp>
// clang-format on
#endif

namespace coro::net::tcp
{
using namespace std::chrono_literals;

client::client(std::shared_ptr<io_scheduler> scheduler, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_socket(
          net::make_socket(
              net::socket::options{m_options.address.domain(), net::socket::type_t::tcp, net::socket::blocking_t::no}))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp::client cannot have nullptr io_scheduler"};
    }

#if defined(CORO_PLATFORM_WINDOWS)
    // Bind socket to IOCP
    m_io_scheduler->bind_socket(m_socket);
#endif
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

#if defined(CORO_PLATFORM_WINDOWS)
    // Bind socket to IOCP
    m_io_scheduler->bind_socket(m_socket);
#endif
}

client::client(client&& other) noexcept
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_options(std::move(other.m_options)),
      m_socket(std::move(other.m_socket)),
      m_connect_status(std::exchange(other.m_connect_status, std::nullopt))
{
}

client::~client()
{
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

#if defined(CORO_PLATFORM_UNIX)
client::client(const client& other)
    : m_io_scheduler(other.m_io_scheduler),
      m_options(other.m_options),
      m_socket(other.m_socket),
      m_connect_status(other.m_connect_status)
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
#endif

auto client::connect(std::chrono::milliseconds timeout) -> coro::task<connect_status>
{
    // Only allow the user to connect per tcp client once, if they need to re-connect they should
    // make a new tcp::client.
    if (m_connect_status.has_value())
    {
        co_return m_connect_status.value();
    }

    // This enforces the connection status is always set on the client object upon returning.
    auto return_value = [this](connect_status s) -> connect_status
    {
        m_connect_status = s;
        return s;
    };

    sockaddr_storage server_storage{};
    std::size_t      server_length{};
    m_options.address.to_os(m_options.port, server_storage, server_length);

#if defined(CORO_PLATFORM_UNIX)
    auto cret = ::connect(m_socket.native_handle(), reinterpret_cast<struct sockaddr*>(&server_storage), server_length);
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
#elif defined(CORO_PLATFORM_WINDOWS)
    // TODO: function to retrieve pointers
    static LPFN_CONNECTEX connect_ex_function;
    static std::once_flag connect_ex_function_created;
    std::call_once(
        connect_ex_function_created,
        [this]
        {
            DWORD num_bytes{};

            GUID guid    = WSAID_CONNECTEX;
            int  success = ::WSAIoctl(
                reinterpret_cast<SOCKET>(m_socket.native_handle()),
                SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guid,
                sizeof(guid),
                &connect_ex_function,
                sizeof(connect_ex_function),
                &num_bytes,
                NULL,
                NULL);

            if (success != 0 || !connect_ex_function)
                throw std::runtime_error("Failed to retrieve GetAcceptExSockaddrs function pointer");
        });

    detail::overlapped_io_operation ovpi{};
    ovpi.socket = reinterpret_cast<SOCKET>(m_socket.native_handle());

    // Bind socket first to local address
    sockaddr_storage local_addr_storage{};
    std::size_t      local_addr_length{};
    ip_address::get_any_address(m_options.address.domain()).to_os(0, local_addr_storage, local_addr_length);

    if (bind(
            reinterpret_cast<SOCKET>(m_socket.native_handle()),
            reinterpret_cast<struct sockaddr*>(&local_addr_storage),
            local_addr_length) == SOCKET_ERROR)
    {
        co_return return_value(connect_status::error);
    }

    // Now connect
    BOOL result = connect_ex_function(
        reinterpret_cast<SOCKET>(m_socket.native_handle()),
        reinterpret_cast<sockaddr*>(&server_storage),
        server_length,
        nullptr,
        0,
        nullptr,
        &ovpi.ov);

    if (!result)
    {
        const DWORD err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            co_return return_value(connect_status::error);
        }
    }

    auto status = result ? poll_status::event : co_await m_io_scheduler->poll(ovpi.pi, timeout);

    if (status == poll_status::event)
    {
        int error     = 0;
        int error_len = sizeof(error);
        if (getsockopt(
                reinterpret_cast<SOCKET>(m_socket.native_handle()),
                SOL_SOCKET,
                SO_ERROR,
                reinterpret_cast<char*>(&error),
                &error_len) != 0 ||
            error != 0)
        {
            co_return return_value(connect_status::error);
        }
        setsockopt(
            reinterpret_cast<SOCKET>(m_socket.native_handle()), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
        co_return return_value(connect_status::connected);
    }
    else if (status == poll_status::timeout)
    {
        CancelIoEx((HANDLE)m_socket.native_handle(), &ovpi.ov);
        co_return return_value(connect_status::timeout);
    }

    co_return return_value(connect_status::error);
#endif
}

#if defined(CORO_PLATFORM_UNIX)
auto client::poll(coro::poll_op op, std::chrono::milliseconds timeout) -> coro::task<poll_status>
{
    return m_io_scheduler->poll(m_socket, op, timeout);
}
#elif defined(CORO_PLATFORM_WINDOWS)

auto client::write(std::span<const char> buffer, std::chrono::milliseconds timeout)
    -> task<std::pair<write_status, std::span<const char>>>
{
    static constexpr auto send_fn = [](SOCKET s, detail::overlapped_io_operation& ov, WSABUF& buf)
    { return WSASend(s, &buf, 1, &ov.bytes_transferred, 0, &ov.ov, nullptr); };

    co_return co_await detail::perform_write_read_operation<write_status, std::span<const char>, false>(
        m_io_scheduler, reinterpret_cast<SOCKET>(m_socket.native_handle()), send_fn, buffer, timeout);
}

auto client::read(std::span<char> buffer, std::chrono::milliseconds timeout)
    -> task<std::pair<read_status, std::span<char>>>
{
    static constexpr auto recv_fn = [](SOCKET s, detail::overlapped_io_operation& ov, WSABUF& buf)
    {
        DWORD flags{};
        return WSARecv(s, &buf, 1, &ov.bytes_transferred, &flags, &ov.ov, nullptr);
    };

    co_return co_await detail::perform_write_read_operation<read_status, std::span<char>, true>(
        m_io_scheduler, reinterpret_cast<SOCKET>(m_socket.native_handle()), recv_fn, buffer, timeout);
}
#endif

} // namespace coro::net::tcp
