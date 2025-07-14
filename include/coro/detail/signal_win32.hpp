#pragma once
#include <atomic>
#include <memory>

namespace coro::detail
{
class signal_win32
{
    struct Event;
    friend class io_notifier_iocp;

public:
    signal_win32();
    ~signal_win32();

    void set();
    void unset();

private:
    void* m_iocp{};
    void* m_data{};
};
} // namespace coro::detail