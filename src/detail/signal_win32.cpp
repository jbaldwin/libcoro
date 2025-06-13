#include <coro/detail/signal_win32.hpp>
#include <Windows.h>

namespace coro::detail
{
// Maybe not thread-safe
struct signal_win32::Event
{
    OVERLAPPED          overlapped;
    void*               data;
    bool                is_set;
};

signal_win32::signal_win32() : m_event(std::make_unique<Event>())
{

}
signal_win32::~signal_win32()
{
}
void signal_win32::set()
{
    m_event->is_set = true;
    m_event->data = m_data;
    PostQueuedCompletionStatus(m_iocp, 0, (ULONG_PTR)signal_key, (LPOVERLAPPED)(void*)m_event.get());
}
void signal_win32::unset()
{
    m_event->is_set = false;
    m_event->data = m_data;
    PostQueuedCompletionStatus(m_iocp, 0, (ULONG_PTR)signal_key, (LPOVERLAPPED)(void*)m_event.get());
}
}