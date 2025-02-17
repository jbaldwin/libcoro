#ifdef LIBCORO_FEATURE_TLS

    #include "coro/net/tls/client.hpp"

namespace coro::net::tls
{
using namespace std::chrono_literals;

client::client(std::shared_ptr<io_scheduler> scheduler, std::shared_ptr<context> tls_ctx, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_tls_ctx(std::move(tls_ctx)),
      m_options(std::move(opts)),
      m_socket(net::make_socket(
          net::socket::options{m_options.address.domain(), net::socket::type_t::tcp, net::socket::blocking_t::no}))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tls::client cannot have nullptr io_scheduler"};
    }

    if (m_tls_ctx == nullptr)
    {
        throw std::runtime_error{"tls::client cannot have nullptr tls_ctx"};
    }
}

client::client(
    std::shared_ptr<io_scheduler> scheduler, std::shared_ptr<context> tls_ctx, net::socket socket, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_tls_ctx(std::move(tls_ctx)),
      m_options(std::move(opts)),
      m_socket(std::move(socket)),
      m_connect_status(connection_status::connected),
      m_tls_info(tls_connection_type::accept)
{
    // io_scheduler is assumed good since it comes from a tls::server.
    // tls_ctx is assumed good since it comes from a tls::server.

    // Force the socket to be non-blocking.
    m_socket.blocking(coro::net::socket::blocking_t::no);
}

client::client(client&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_tls_ctx(std::move(other.m_tls_ctx)),
      m_options(std::move(other.m_options)),
      m_socket(std::move(other.m_socket)),
      m_connect_status(std::exchange(other.m_connect_status, std::nullopt)),
      m_tls_info(std::move(other.m_tls_info))
{
}

client::~client()
{
    // If this tcp client is using SSL and the connection did not have an ssl error, schedule a task
    // to shutdown the connection cleanly.  This is done on a background scheduled task since the
    // tcp client's destructor cannot co_await the SSL_shutdown() read and write poll operations.
    if (m_tls_info.m_tls_ptr != nullptr && !m_tls_info.m_tls_error)
    {
        // Should the shutdown timeout be configurable?
        m_io_scheduler->spawn(tls_shutdown_and_free(
            m_io_scheduler, std::move(m_socket), std::move(m_tls_info.m_tls_ptr), std::chrono::seconds{30}));
    }
}

auto client::operator=(client&& other) noexcept -> client&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler   = std::move(other.m_io_scheduler);
        m_tls_ctx        = std::move(other.m_tls_ctx);
        m_options        = std::move(other.m_options);
        m_socket         = std::move(other.m_socket);
        m_connect_status = std::exchange(other.m_connect_status, std::nullopt);
        m_tls_info       = std::move(other.m_tls_info);
    }
    return *this;
}

auto client::connect(std::chrono::milliseconds timeout) -> coro::task<connection_status>
{
    // Only allow the user to connect per tcp client once, if they need to re-connect they should
    // make a new tls::client.
    if (m_connect_status.has_value())
    {
        co_return m_connect_status.value();
    }

    // tls context isn't setup and is required.
    if (m_tls_ctx == nullptr)
    {
        co_return connection_status::context_required;
    }

    // This enforces the connection status is aways set on the client object upon returning.
    auto return_value = [this](connection_status s) -> connection_status
    {
        m_connect_status = s;
        return s;
    };

    sockaddr_in server{};
    server.sin_family = static_cast<int>(m_options.address.domain());
    server.sin_port   = htons(m_options.port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(m_options.address.data().data());

    auto cret = ::connect(m_socket.native_handle(), (struct sockaddr*)&server, sizeof(server));
    if (cret == 0)
    {
        co_return return_value(co_await handshake(timeout));
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
                    // TODO: delta the already used time and remove from the handshake timeout.
                    co_return return_value(co_await handshake(timeout));
                }
            }
            else if (pstatus == poll_status::timeout)
            {
                co_return return_value(connection_status::timeout);
            }
        }
    }

    co_return return_value(connection_status::error);
}

auto client::handshake(std::chrono::milliseconds timeout) -> coro::task<connection_status>
{
    m_tls_info.m_tls_ptr = tls_unique_ptr{SSL_new(m_tls_ctx->native_handle())};
    if (m_tls_info.m_tls_ptr == nullptr)
    {
        co_return connection_status::resource_allocation_failed;
    }

    auto* tls = m_tls_info.m_tls_ptr.get();

    if (auto r = SSL_set_fd(tls, m_socket.native_handle()); r == 0)
    {
        co_return connection_status::set_fd_failure;
    }

    if (m_tls_info.m_tls_connection_type == tls_connection_type::connect)
    {
        SSL_set_connect_state(tls);
    }
    else // ssl_connection_type::accept
    {
        SSL_set_accept_state(tls);
    }

    int r{0};
    ERR_clear_error();
    while ((r = SSL_connect(tls)) != 1)
    {
        poll_op op{poll_op::read_write};
        int     err = SSL_get_error(tls, r);
        if (err == SSL_ERROR_WANT_WRITE)
        {
            op = poll_op::write;
        }
        else if (err == SSL_ERROR_WANT_READ)
        {
            op = poll_op::read;
        }
        else
        {
            // char error_buffer[256];
            // ERR_error_string(err, error_buffer);
            // std::cerr << "ssl_handleshake error=[" << error_buffer << "]\n";
            co_return connection_status::handshake_failed;
        }

        // TODO: adjust timeout based on elapsed time so far.
        auto pstatus = co_await m_io_scheduler->poll(m_socket, op, timeout);
        switch (pstatus)
        {
            case poll_status::timeout:
                co_return connection_status::timeout;
            case poll_status::error:
                co_return connection_status::poll_error;
            case poll_status::closed:
                co_return connection_status::unexpected_close;
            default:
                // Event triggered, continue handshake.
                break;
        }
    }

    co_return connection_status::connected;
}

auto client::tls_shutdown_and_free(
    std::shared_ptr<io_scheduler> io_scheduler,
    net::socket                   s,
    tls_unique_ptr                tls_ptr,
    std::chrono::milliseconds     timeout) -> coro::task<void>
{
    while (true)
    {
        ERR_clear_error();
        auto r = SSL_shutdown(tls_ptr.get());
        if (r == 1) // shutdown complete
        {
            co_return;
        }
        else if (r == 0) // shutdown in progress
        {
            coro::poll_op op{coro::poll_op::read_write};
            auto          err = SSL_get_error(tls_ptr.get(), r);
            if (err == SSL_ERROR_WANT_WRITE)
            {
                op = coro::poll_op::write;
            }
            else if (err == SSL_ERROR_WANT_READ)
            {
                op = coro::poll_op::read;
            }
            else
            {
                co_return;
            }

            auto pstatus = co_await io_scheduler->poll(s, op, timeout);
            switch (pstatus)
            {
                case poll_status::timeout:
                case poll_status::error:
                case poll_status::closed:
                    co_return;
                default:
                    // continue shutdown.
                    break;
            }
        }
        else // r < 0 error
        {
            co_return;
        }
    }
}

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS
