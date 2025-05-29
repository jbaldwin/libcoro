#pragma once

#include <mutex>

extern std::mutex g_catch2_thread_safe_mutex;
#define REQUIRE_THREAD_SAFE(expr) {std::lock_guard<std::mutex> lock_guard{g_catch2_thread_safe_mutex}; REQUIRE(expr);}
