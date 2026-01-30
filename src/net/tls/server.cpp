#ifdef LIBCORO_FEATURE_TLS

    #include "coro/net/tls/server.hpp"

    #include "coro/io_scheduler.hpp"

namespace coro::net::tls
{
server::server(
    std::unique_ptr<coro::io_scheduler>& scheduler,
    std::shared_ptr<context>             tls_ctx,
    const net::socket_address&                 endpoint,
    options                              opts)
    : m_io_scheduler(scheduler.get()),
      m_tls_ctx(std::move(tls_ctx)),
      m_options(std::move(opts)),
      m_accept_socket(
          net::make_accept_socket(
              net::socket::options{net::socket::type_t::tcp, net::socket::blocking_t::no}, endpoint, m_options.backlog))
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
    : m_io_scheduler(std::exchange(other.m_io_scheduler, nullptr)),
      m_tls_ctx(std::move(other.m_tls_ctx)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto server::operator=(server&& other) -> server&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler  = std::exchange(other.m_io_scheduler, nullptr);
        m_tls_ctx       = std::move(other.m_tls_ctx);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

auto server::accept(std::chrono::milliseconds timeout) -> coro::task<coro::net::tls::client>
{
    auto client_endpoint = net::socket_address::make_uninitialised();
    auto [addr, len]     = client_endpoint.native_mutable_data();

    net::socket s{::accept(m_accept_socket.native_handle(), addr, len)};

    auto tls_client = tls::client{m_io_scheduler, m_tls_ctx, std::move(s), client_endpoint};

    auto hstatus = co_await tls_client.handshake(timeout);
    (void)hstatus; // user must check result.
    co_return std::move(tls_client);
};

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS
