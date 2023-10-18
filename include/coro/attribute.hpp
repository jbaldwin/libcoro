#pragma once

// This is a GCC extension; define it only for GCC and compilers that emulate GCC.
#if defined(__GNUC__) && !defined(__clang__)
    #define __ATTRIBUTE__(attr) __attribute__((attr))
#else
    #define __ATTRIBUTE__(attr)
#endif
