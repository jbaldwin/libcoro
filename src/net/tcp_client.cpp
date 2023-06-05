#include "coro/net/tcp_client.hpp"

namespace coro::net
{
using namespace std::chrono_literals;

tcp_client::tcp_client(std::shared_ptr<io_scheduler> scheduler, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_socket(net::make_socket(
          net::socket::options{m_options.address.domain(), net::socket::type_t::tcp, net::socket::blocking_t::no}))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp_client cannot have nullptr io_scheduler"};
    }
}

tcp_client::tcp_client(std::shared_ptr<io_scheduler> scheduler, net::socket socket, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_socket(std::move(socket)),
      m_connect_status(connect_status::connected)
#ifdef LIBCORO_FEATURE_SSL
      ,
      m_ssl_info(ssl_connection_type::accept)
#endif
{
    // io_scheduler is assumed good since it comes from a tcp_server.

    // Force the socket to be non-blocking.
    m_socket.blocking(coro::net::socket::blocking_t::no);
}

tcp_client::tcp_client(tcp_client&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_options(std::move(other.m_options)),
      m_socket(std::move(other.m_socket)),
      m_connect_status(std::exchange(other.m_connect_status, std::nullopt))
#ifdef LIBCORO_FEATURE_SSL
      ,
      m_ssl_info(std::move(other.m_ssl_info))
#endif
{
}

tcp_client::~tcp_client()
{
#ifdef LIBCORO_FEATURE_SSL
    // If this tcp client is using SSL and the connection did not have an ssl error, schedule a task
    // to shutdown the connection cleanly.  This is done on a background scheduled task since the
    // tcp client's destructor cannot co_await the SSL_shutdown() read and write poll operations.
    if (m_ssl_info.m_ssl_ptr != nullptr && !m_ssl_info.m_ssl_error)
    {
        // Should the shutdown timeout be configurable?
        m_io_scheduler->schedule(ssl_shutdown_and_free(
            m_io_scheduler, std::move(m_socket), std::move(m_ssl_info.m_ssl_ptr), std::chrono::seconds{30}));
    }
#endif
}

auto tcp_client::operator=(tcp_client&& other) noexcept -> tcp_client&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler   = std::move(other.m_io_scheduler);
        m_options        = std::move(other.m_options);
        m_socket         = std::move(other.m_socket);
        m_connect_status = std::exchange(other.m_connect_status, std::nullopt);
#ifdef LIBCORO_FEATURE_SSL
        m_ssl_info       = std::move(other.m_ssl_info);
#endif
    }
    return *this;
}

auto tcp_client::connect(std::chrono::milliseconds timeout) -> coro::task<connect_status>
{
    // Only allow the user to connect per tcp client once, if they need to re-connect they should
    // make a new tcp_client.
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

    auto cret = ::connect(m_socket.native_handle(), (struct sockaddr*)&server, sizeof(server));
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

#ifdef LIBCORO_FEATURE_SSL
auto tcp_client::ssl_handshake(std::chrono::milliseconds timeout) -> coro::task<ssl_handshake_status>
{
    if (!m_connect_status.has_value() || m_connect_status.value() != connect_status::connected)
    {
        // Can't ssl handshake if the connection isn't established.
        co_return ssl_handshake_status::not_connected;
    }

    if (m_options.ssl_ctx == nullptr)
    {
        // ssl isn't setup
        co_return ssl_handshake_status::ssl_context_required;
    }

    if (m_ssl_info.m_ssl_handshake_status.has_value())
    {
        // The user has already called this function.
        co_return m_ssl_info.m_ssl_handshake_status.value();
    }

    // Enforce on any return past here to set the cached handshake status.
    auto return_value = [this](ssl_handshake_status s) -> ssl_handshake_status
    {
        m_ssl_info.m_ssl_handshake_status = s;
        return s;
    };

    m_ssl_info.m_ssl_ptr = ssl_unique_ptr{SSL_new(m_options.ssl_ctx->native_handle())};
    if (m_ssl_info.m_ssl_ptr == nullptr)
    {
        co_return return_value(ssl_handshake_status::ssl_resource_allocation_failed);
    }

    if (auto r = SSL_set_fd(m_ssl_info.m_ssl_ptr.get(), m_socket.native_handle()); r == 0)
    {
        co_return return_value(ssl_handshake_status::ssl_set_fd_failure);
    }

    if (m_ssl_info.m_ssl_connection_type == ssl_connection_type::connect)
    {
        SSL_set_connect_state(m_ssl_info.m_ssl_ptr.get());
    }
    else // ssl_connection_type::accept
    {
        SSL_set_accept_state(m_ssl_info.m_ssl_ptr.get());
    }

    int r{0};
    ERR_clear_error();
    while ((r = SSL_do_handshake(m_ssl_info.m_ssl_ptr.get())) != 1)
    {
        poll_op op{poll_op::read_write};
        int     err = SSL_get_error(m_ssl_info.m_ssl_ptr.get(), r);
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
            co_return return_value(ssl_handshake_status::handshake_failed);
        }

        // TODO: adjust timeout based on elapsed time so far.
        auto pstatus = co_await m_io_scheduler->poll(m_socket, op, timeout);
        switch (pstatus)
        {
            case poll_status::timeout:
                co_return return_value(ssl_handshake_status::timeout);
            case poll_status::error:
                co_return return_value(ssl_handshake_status::poll_error);
            case poll_status::closed:
                co_return return_value(ssl_handshake_status::unexpected_close);
            default:
                // Event triggered, continue handshake.
                break;
        }
    }

    co_return return_value(ssl_handshake_status::ok);
}

auto tcp_client::ssl_shutdown_and_free(
    std::shared_ptr<io_scheduler> io_scheduler,
    net::socket                   s,
    ssl_unique_ptr                ssl_ptr,
    std::chrono::milliseconds     timeout) -> coro::task<void>
{
    while (true)
    {
        auto r = SSL_shutdown(ssl_ptr.get());
        if (r == 1) // shutdown complete
        {
            co_return;
        }
        else if (r == 0) // shutdown in progress
        {
            coro::poll_op op{coro::poll_op::read_write};
            auto          err = SSL_get_error(ssl_ptr.get(), r);
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
#endif

} // namespace coro::net
