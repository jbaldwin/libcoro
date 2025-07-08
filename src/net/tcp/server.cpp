#include "coro/net/tcp/server.hpp"

#include "coro/io_scheduler.hpp"

// The order of includes matters
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2ipdef.h>
#include <MSWSock.h>
#include "coro/detail/iocp_overlapped.hpp"

// clang-format on

namespace coro::net::tcp
{
server::server(std::shared_ptr<io_scheduler> scheduler, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_accept_socket(
          net::make_accept_socket(
              net::socket::options{net::domain_t::ipv4, net::socket::type_t::tcp, net::socket::blocking_t::no},
              m_options.address,
              m_options.port,
              m_options.backlog))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp::server cannot have a nullptr io_scheduler"};
    }
#if defined(CORO_PLATFORM_WINDOWS)
    // Bind socket to IOCP
    m_io_scheduler->bind_socket(m_accept_socket);
#endif
}

server::server(server&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto server::operator=(server&& other) -> server&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler  = std::move(other.m_io_scheduler);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

#if defined(CORO_PLATFORM_UNIX)
auto server::accept() -> coro::net::tcp::client
{
    sockaddr_in         client{};
    constexpr const int len = sizeof(struct sockaddr_in);

    net::socket s{reinterpret_cast<socket::native_handle_t>(::accept(
        reinterpret_cast<SOCKET>(m_accept_socket.native_handle()),
        reinterpret_cast<struct sockaddr*>(&client),
        const_cast<socklen_t*>(reinterpret_cast<const socklen_t*>(&len))))};

    std::span<const uint8_t> ip_addr_view{
        reinterpret_cast<uint8_t*>(&client.sin_addr.s_addr),
        sizeof(client.sin_addr.s_addr),
    };

    return tcp::client{
        m_io_scheduler,
        std::move(s),
        client::options{
            .address = net::ip_address{ip_addr_view, static_cast<net::domain_t>(client.sin_family)},
            .port    = ntohs(client.sin_port),
        }};
};

auto server::accept_client() -> coro::task<std::optional<coro::net::tcp::client>>
{
}
#elif defined(CORO_PLATFORM_WINDOWS)
auto server::accept_client(const std::chrono::milliseconds timeout) -> coro::task<std::optional<coro::net::tcp::client>>
{
    static LPFN_ACCEPTEX  accept_ex_function;
    static std::once_flag accept_ex_function_created;

    std::call_once(
        accept_ex_function_created,
        [this]
        {
            DWORD num_bytes{};
            GUID  guid = WSAID_ACCEPTEX;

            int success = ::WSAIoctl(
                reinterpret_cast<SOCKET>(m_accept_socket.native_handle()),
                SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guid,
                sizeof(guid),
                &accept_ex_function,
                sizeof(accept_ex_function),
                &num_bytes,
                nullptr,
                nullptr);

            if (success != 0 || !accept_ex_function)
                throw std::runtime_error("Failed to retrieve AcceptEx function pointer");
        });


    static LPFN_GETACCEPTEXSOCKADDRS  get_accept_ex_sock_addrs_function;
    static std::once_flag get_accept_ex_sock_addrs_created;
    std::call_once(
        get_accept_ex_sock_addrs_created,
        [this]
        {
            DWORD num_bytes{};
            GUID  guid = WSAID_GETACCEPTEXSOCKADDRS;

            int success = ::WSAIoctl(
                reinterpret_cast<SOCKET>(m_accept_socket.native_handle()),
                SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guid,
                sizeof(guid),
                &get_accept_ex_sock_addrs_function,
                sizeof(get_accept_ex_sock_addrs_function),
                &num_bytes,
                nullptr,
                nullptr);

            if (success != 0 || !get_accept_ex_sock_addrs_function)
                throw std::runtime_error("Failed to retrieve GetAcceptExSockaddrs function pointer");
        });

    detail::overlapped_io_operation ovpi{.is_accept = true};

    auto client = net::make_socket(
        socket::options{
            .domain = m_options.address.domain(), .type = socket::type_t::tcp, .blocking = socket::blocking_t::no});

    // AcceptEx requires a buffer for local + remote address, also extra 32 bytes because MS recommends so
    char accept_buffer[(sizeof(SOCKADDR_STORAGE) + 16) * 2] = {};

    DWORD bytes_received = 0;
    BOOL  result         = accept_ex_function(
        reinterpret_cast<SOCKET>(m_accept_socket.native_handle()),
        reinterpret_cast<SOCKET>(client.native_handle()),
        accept_buffer,
        0,
        sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16,
        &bytes_received,
        &ovpi.ov);

    if (!result)
    {
        const DWORD err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            co_return std::nullopt;
        }
    }

    auto status = result ? poll_status::event : (co_await m_io_scheduler->poll(ovpi.pi, timeout));

    if (status == poll_status::event)
    {
        // 1) Обновляем контекст принятого сокета
        SOCKET listen_handle = reinterpret_cast<SOCKET>(m_accept_socket.native_handle());
        ::setsockopt(
            reinterpret_cast<SOCKET>(client.native_handle()),
            SOL_SOCKET,
            SO_UPDATE_ACCEPT_CONTEXT,
            reinterpret_cast<const char*>(&listen_handle),
            sizeof(listen_handle));

        // 2) Забираем адреса из accept_buffer
        SOCKADDR* local_addr  = nullptr;
        SOCKADDR* remote_addr = nullptr;
        int       local_len   = 0;
        int       remote_len  = 0;

        get_accept_ex_sock_addrs_function(
            accept_buffer,
            0,
            sizeof(SOCKADDR_IN) + 16,
            sizeof(SOCKADDR_IN) + 16,
            &local_addr,
            &local_len,
            &remote_addr,
            &remote_len);

        // 3) Распаковываем remote_addr → ip + порт
        auto            domain = remote_addr->sa_family == AF_INET ? domain_t::ipv4 : domain_t::ipv6;
        net::ip_address address;
        uint16_t        port;

        if (domain == domain_t::ipv4)
        {
            auto* sin = reinterpret_cast<SOCKADDR_IN*>(remote_addr);
            address   = net::ip_address{
                std::span{reinterpret_cast<const uint8_t*>(&sin->sin_addr), sizeof(sin->sin_addr)}, domain_t::ipv4};
            port = ntohs(sin->sin_port);
        }
        else
        {
            auto* sin6 = reinterpret_cast<SOCKADDR_IN6*>(remote_addr);
            address    = net::ip_address{
                std::span{reinterpret_cast<const uint8_t*>(&sin6->sin6_addr), sizeof(sin6->sin6_addr)}, domain_t::ipv6};
            port = ntohs(sin6->sin6_port);
        }

        co_return coro::net::tcp::client{m_io_scheduler, std::move(client), client::options{address, port}};
    }

    co_return std::nullopt;
}

#endif

} // namespace coro::net::tcp
