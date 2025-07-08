#pragma once
#include <memory>
#include <mutex>

namespace coro::detail
{
/**
 * @brief RAII wrapper for WinSock (WSAStartup/WSACleanup)
 *
 * Ensures that WinSock is initialised once and is automatically cleaned up when the last user
 * releases its reference.
 *
 * It is constructed via the `initialise_winsock()`.
 *
 * If it has been already cleaned up then future calls to `initialise_winsock()` 
 * will reinitialize WinSock.
 */
class winsock_handle
{
    struct private_constructor
    {
    };

public:
    winsock_handle(private_constructor);
    ~winsock_handle();

    winsock_handle(const winsock_handle&)                    = delete;
    auto operator=(const winsock_handle&) -> winsock_handle& = delete;

    winsock_handle(winsock_handle&&)                    = delete;
    auto operator=(winsock_handle&&) -> winsock_handle& = delete;

    friend auto initialise_winsock() -> std::shared_ptr<winsock_handle>;

private:
    static inline std::mutex                    mutex;
    static inline std::weak_ptr<winsock_handle> current_winsock_handle;
};

auto initialise_winsock() -> std::shared_ptr<winsock_handle>;
}