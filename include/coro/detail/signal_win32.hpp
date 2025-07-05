#pragma once
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
    mutable void*                  m_iocp{};
    mutable void*                  m_data{};
    std::unique_ptr<Event> m_event;
};
} // namespace coro::detail