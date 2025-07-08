#include <coro/detail/winsock_handle.hpp> 
#include <WinSock2.h>
#include <Windows.h>
#include <stdexcept>
#include <string>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "wsock32.lib")

namespace coro::detail
{
coro::detail::winsock_handle::winsock_handle(private_constructor)
{
    WSADATA data;
    int     r = WSAStartup(MAKEWORD(2, 2), &data);
    if (r != 0)
    {
        throw std::runtime_error{"WSAStartup failed: " + std::to_string(r)};
    }
}

coro::detail::winsock_handle::~winsock_handle()
{
    WSACleanup();
}

auto initialise_winsock() -> std::shared_ptr<winsock_handle>
{
    std::unique_lock lk{winsock_handle::mutex};

    std::shared_ptr<winsock_handle> handle = winsock_handle::current_winsock_handle.lock();
    if (!handle)
    {
        handle = std::make_shared<winsock_handle>(winsock_handle::private_constructor{});
        winsock_handle::current_winsock_handle = handle;
    }

    return handle;
}
}