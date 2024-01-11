#include "coro/net/dns/resolver.hpp"

namespace coro::net::dns
{
uint64_t   m_ares_count{0};
std::mutex m_ares_mutex{};
} // namespace coro::net::dns
