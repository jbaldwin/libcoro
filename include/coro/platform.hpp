#pragma once

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #define CORO_PLATFORM_UNIX
    #define CORO_PLATFORM_BSD
#elif defined(__linux__)
    #define CORO_PLATFORM_UNIX
    #define CORO_PLATFORM_LINUX
#elif defined(_WIN32) || defined(_WIN64)
    #define CORO_PLATFORM_WINDOWS
#endif