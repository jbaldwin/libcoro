#pragma once

#include "coro/poll.hpp"
#include <cstddef>
#include <string>

namespace coro::net
{

struct io_status
{
    enum class kind
    {
        ok,
        closed,
        connection_reset,
        connection_refused,
        timeout,

        try_again,
        polling_error,
        cancelled,

        native
    };

    kind type{};
    int  native_code{};

    [[nodiscard]] auto is_ok() const -> bool { return type == kind::ok; }
    [[nodiscard]] auto is_timeout() const -> bool { return type == kind::timeout; }
    [[nodiscard]] auto is_closed() const -> bool { return type == kind::closed; }

    [[nodiscard]] auto is_native() const -> bool { return type == kind::native; }

    explicit operator bool() const { return is_ok(); }

    /**
     * Returns a human-readable description of the error.
     */
    [[nodiscard]] auto message() const -> std::string;
};

auto make_io_status_from_native(int native_code) -> io_status;
auto make_io_status_poll_status(coro::poll_status status) -> io_status;
} // namespace coro::net