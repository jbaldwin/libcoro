#pragma once

#include "coro/concepts/awaitable.hpp"

#include <atomic>

namespace coro::detail
{

template<concepts::detail::awaiter_forward_list_entry awaiter_type>
auto awaiter_list_push(std::atomic<awaiter_type*>& list, awaiter_type* to_enqueue) -> void
{
    awaiter_type* current = list.load(std::memory_order::acquire);
    do
    {
        to_enqueue->m_next = current;
    } while (!list.compare_exchange_weak(
        current, to_enqueue, std::memory_order::acq_rel, std::memory_order::acquire));
}

template<concepts::detail::awaiter_forward_list_entry awaiter_type>
auto awaiter_list_pop(std::atomic<awaiter_type*>& list) -> awaiter_type*
{
    awaiter_type* waiter = list.load(std::memory_order::acquire);
    do
    {
        if (waiter == nullptr)
        {
            return nullptr;
        }
    } while (!list.compare_exchange_weak(
        waiter, waiter->m_next, std::memory_order::acq_rel, std::memory_order::acquire));

    return waiter;
}

template<concepts::detail::awaiter_forward_list_entry awaiter_type>
auto awaiter_list_pop_all(std::atomic<awaiter_type*>& list) -> awaiter_type*
{
    awaiter_type* head = list.load(std::memory_order::acquire);

    do
    {
        // The list has become empty.
        if (head == nullptr)
        {
            break;
        }
    } while (!list.compare_exchange_weak(
        head, nullptr, std::memory_order::acq_rel, std::memory_order::acquire));

    return head;
}

template<concepts::detail::awaiter_forward_list_entry awaiter_type>
auto awaiter_list_reverse(awaiter_type* curr) -> awaiter_type*
{
    if (curr == nullptr || curr->m_next == nullptr)
    {
        return curr;
    }

    awaiter_type* prev = nullptr;
    awaiter_type* next = nullptr;
    while (curr != nullptr)
    {
        next         = curr->m_next;
        curr->m_next = prev;
        prev         = curr;
        curr         = next;
    }

    return prev;
}


} // namespace coro::detail
