#pragma once

// This is a GCC extension; define it only for GCC and compilers that emulate GCC.
#ifdef __GNUC__
    #define __ATTRIBUTE__(attr) __attribute__((attr))
#else
    #define __ATTRIBUTE__(attr)
#endif
