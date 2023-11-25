#include "coro/net/recv_status.hpp"

namespace coro::net
{
static const std::string recv_status_ok{"ok"};
static const std::string recv_status_closed{"closed"};
static const std::string recv_status_udp_not_bound{"udp_not_bound"};
static const std::string recv_status_would_block{"would_block"};
static const std::string recv_status_bad_file_descriptor{"bad_file_descriptor"};
static const std::string recv_status_connection_refused{"connection_refused"};
static const std::string recv_status_memory_fault{"memory_fault"};
static const std::string recv_status_interrupted{"interrupted"};
static const std::string recv_status_invalid_argument{"invalid_argument"};
static const std::string recv_status_no_memory{"no_memory"};
static const std::string recv_status_not_connected{"not_connected"};
static const std::string recv_status_not_a_socket{"not_a_socket"};
static const std::string recv_status_unknown{"unknown"};

auto to_string(recv_status status) -> const std::string&
{
    switch (status)
    {
        case recv_status::ok:
            return recv_status_ok;
        case recv_status::closed:
            return recv_status_closed;
        case recv_status::udp_not_bound:
            return recv_status_udp_not_bound;
        // case recv_status::try_again: return recv_status_try_again;
        case recv_status::would_block:
            return recv_status_would_block;
        case recv_status::bad_file_descriptor:
            return recv_status_bad_file_descriptor;
        case recv_status::connection_refused:
            return recv_status_connection_refused;
        case recv_status::memory_fault:
            return recv_status_memory_fault;
        case recv_status::interrupted:
            return recv_status_interrupted;
        case recv_status::invalid_argument:
            return recv_status_invalid_argument;
        case recv_status::no_memory:
            return recv_status_no_memory;
        case recv_status::not_connected:
            return recv_status_not_connected;
        case recv_status::not_a_socket:
            return recv_status_not_a_socket;
    }

    return recv_status_unknown;
}

} // namespace coro::net
