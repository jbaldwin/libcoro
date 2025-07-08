#include <coro/detail/signal_win32.hpp>
#include <coro/io_notifier.hpp>
#include <Windows.h>

namespace coro::detail
{

signal_win32::signal_win32()
{

}
signal_win32::~signal_win32()
{
}
void signal_win32::set()
{
    printf("Set signal %p\n", m_data);
    PostQueuedCompletionStatus(
        m_iocp, 
        0, 
        static_cast<int>(io_notifier::completion_key::signal_set),
        (LPOVERLAPPED)m_data
    );
}
void signal_win32::unset()
{
    printf("Unset signal %p\n", m_data);
    PostQueuedCompletionStatus(
        m_iocp, 
        0, 
        static_cast<int>(io_notifier::completion_key::signal_unset),
        (LPOVERLAPPED)m_data
    );
}
}