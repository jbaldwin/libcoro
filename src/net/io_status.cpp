#include "coro/net/io_status.hpp"
#include <system_error>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
#endif

std::string coro::net::io_status::message() const
{
    if (not is_native())
    {
        return std::string{to_string(type)};
    }

    if (native_code == 0)
    {
        return "Success";
    }

#if defined(_WIN32)
    char*  buffer = nullptr;
    size_t size   = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(native_code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    if (size > 0 && buffer)
    {
        std::string msg(buffer, size);
        LocalFree(buffer);
        return msg;
    }
#else // Linux/BSD
    try
    {
        return std::system_category().message(native_code);
    }
    catch (...)
    {
        return "Unknown system error" + std::to_string(native_code) + ")";
    }
#endif
}

auto coro::net::make_io_status_from_native(int native_code) -> coro::net::io_status
{
#if defined(_WIN32)
    #error "TODO: WIN32"
#else // Linux/BSD
    using kind = io_status::kind;
    kind type;
    switch (native_code)
    {
        case 0:
            type = kind::ok;
            break;
        case EOF:
            type = kind::closed;
            break;
        case ECONNREFUSED:
            type = kind::connection_refused;
            break;
        case ECONNRESET:
            type = kind::connection_reset;
            break;
        case EAGAIN:
    #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
    #endif
            type = kind::would_block_or_try_again;
            break;
        case EMSGSIZE:
            type = kind::message_too_big;
            break;
        default:
            type = kind::native;
            break;
    }
    return coro::net::io_status{.type = type, .native_code = native_code};
#endif
}
auto coro::net::make_io_status_from_poll_status(coro::poll_status status) -> coro::net::io_status
{
    switch (status)
    {
        case poll_status::read:
        case poll_status::write:
            return io_status{io_status::kind::ok};
        case poll_status::timeout:
            return io_status{io_status::kind::timeout};
        case poll_status::error:
            return io_status{io_status::kind::polling_error};
        case poll_status::closed:
            return io_status{io_status::kind::closed};
        case poll_status::cancelled:
            return io_status{io_status::kind::cancelled};
    }
}
auto coro::net::to_string(coro::net::io_status::kind k) -> std::string_view
{
    using kind = io_status::kind;
    switch (k)
    {
        case kind::ok:
            return "Success";
        case kind::closed:
            return "Connection closed by peer";
        case kind::connection_reset:
            return "Connection reset by peer";
        case kind::connection_refused:
            return "Connection refused by target host";
        case kind::timeout:
            return "Operation timed out";
        case kind::would_block_or_try_again:
            return "Operation would block or try again";
        case kind::polling_error:
            return "Polling error";
        case kind::cancelled:
            return "Operation cancelled";
        case kind::udp_not_bound:
            return "Udp socket is not bound";
        case kind::native:
            return "Native error code";
        case kind::message_too_big:
            return "Message is too big";
    }
}
