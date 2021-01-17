#pragma once

#include <string>

namespace coro::net
{
class hostname
{
public:
    hostname() = default;
    explicit hostname(std::string hn) : m_hostname(std::move(hn)) {}
    hostname(const hostname&) = default;
    hostname(hostname&&)      = default;
    auto operator=(const hostname&) noexcept -> hostname& = default;
    auto operator=(hostname&&) noexcept -> hostname& = default;
    ~hostname()                                      = default;

    auto data() const -> const std::string& { return m_hostname; }

    auto operator<=>(const hostname& other) const { return m_hostname <=> other.m_hostname; }

private:
    std::string m_hostname;
};

} // namespace coro::net
