#include "catch_extensions.hpp"

std::mutex g_catch2_thread_safe_mutex = std::mutex{};
