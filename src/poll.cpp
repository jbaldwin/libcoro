#include <coro/poll.hpp>

namespace coro
{

static const std::string poll_unknown{"unknown"};

static const std::string poll_op_read{"read"};
static const std::string poll_op_write{"write"};
static const std::string poll_op_read_write{"read_write"};

auto to_string(poll_op op) -> const std::string&
{
    switch (op)
    {
        case poll_op::read:
            return poll_op_read;
        case poll_op::write:
            return poll_op_write;
        case poll_op::read_write:
            return poll_op_read_write;
        default:
            return poll_unknown;
    }
}

static const std::string poll_status_event{"event"};
static const std::string poll_status_timeout{"timeout"};
static const std::string poll_status_error{"error"};
static const std::string poll_status_closed{"closed"};

auto to_string(poll_status status) -> const std::string&
{
    switch (status)
    {
        case poll_status::event:
            return poll_status_event;
        case poll_status::timeout:
            return poll_status_timeout;
        case poll_status::error:
            return poll_status_error;
        case poll_status::closed:
            return poll_status_closed;
        default:
            return poll_unknown;
    }
}

} // namespace coro
