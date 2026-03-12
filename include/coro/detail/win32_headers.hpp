#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
