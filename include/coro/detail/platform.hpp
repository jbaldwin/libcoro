#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define LIBCORO_PLATFORM_WINDOWS
#endif

#if defined(__linux) || defined(__linux__)
    #define LIBCORO_PLATFORM_LINUX
    #define LIBCORO_PLATFORM_UNIX
#endif

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #define LIBCORO_PLATFORM_BSD
    #define LIBCORO_PLATFORM_UNIX
#endif