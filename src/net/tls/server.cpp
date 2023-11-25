#ifdef LIBCORO_FEATURE_TLS

    #include "coro/net/tls/server.hpp"

    #include "coro/io_scheduler.hpp"

namespace coro::net::tls
{
server::server(std::shared_ptr<io_scheduler> scheduler, std::shared_ptr<context> tls_ctx, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_tls_ctx(std::move(tls_ctx)),
      m_options(std::move(opts)),
      m_accept_socket(net::make_accept_socket(
          net::socket::options{net::domain_t::ipv4, net::socket::type_t::tcp, net::socket::blocking_t::no},
          m_options.address,
          m_options.port,
          m_options.backlog))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tls::server cannot have a nullptr io_scheduler"};
    }

    if (m_tls_ctx == nullptr)
    {
        throw std::runtime_error{"tls::server cannot have a nullptr tls_ctx"};
    }
}

server::server(server&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_tls_ctx(std::move(other.m_tls_ctx)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto server::operator=(server&& other) -> server&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler  = std::move(other.m_io_scheduler);
        m_tls_ctx       = std::move(other.m_tls_ctx);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

auto server::accept(std::chrono::milliseconds timeout) -> coro::task<coro::net::tls::client>
{
    sockaddr_in         client{};
    constexpr const int len = sizeof(struct sockaddr_in);
    net::socket         s{::accept(
        m_accept_socket.native_handle(),
        reinterpret_cast<struct sockaddr*>(&client),
        const_cast<socklen_t*>(reinterpret_cast<const socklen_t*>(&len)))};

    std::span<const uint8_t> ip_addr_view{
        reinterpret_cast<uint8_t*>(&client.sin_addr.s_addr),
        sizeof(client.sin_addr.s_addr),
    };

    auto tls_client = tls::client{
        m_io_scheduler,
        m_tls_ctx,
        std::move(s),
        tls::client::options{
            .address = net::ip_address{ip_addr_view, static_cast<net::domain_t>(client.sin_family)},
            .port    = ntohs(client.sin_port),
        }};

    auto hstatus = co_await tls_client.handshake(timeout);
    (void)hstatus; // user must check result.
    co_return std::move(tls_client);
};

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS
