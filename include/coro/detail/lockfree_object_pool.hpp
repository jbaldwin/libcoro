#pragma once

#include "lockfree_queue.hpp"
#include <memory>

namespace coro::detail
{

template<typename T>
class lockfree_object_pool
{
public:
    lockfree_object_pool() = default;

    template<typename Deleter>
    lockfree_object_pool(
        std::function<std::unique_ptr<T, Deleter>()> creator,
        std::function<void(T*)>                      recycler,
        std::function<void(T*)>                      deleter = std::default_delete<T>())
        : m_creator(creator),
          m_recycler(recycler),
          m_main_deleter(deleter)
    {
    }

    ~lockfree_object_pool()
    {
        while (auto opt = m_free_items.pop())
        {
            if (opt.has_value())
            {
                m_main_deleter(opt.value());
            }
        }
    }

    std::unique_ptr<T, std::function<void(T*)>> acquire()
    {
        auto opt = m_free_items.pop();
        if (opt.has_value())
        {
            return {opt.value(), m_pool_deleter};
        }

        if (!m_creator)
        {
            return {nullptr, m_pool_deleter};
        }

        return {m_creator().release(), m_pool_deleter};
    }

    void release(T* ptr)
    {
        if (m_recycler)
        {
            m_recycler(ptr);
        }
        m_free_items.push(ptr);
    }

    std::function<void(T*)> pool_deleter() const { return m_pool_deleter; }

private:
    std::function<std::unique_ptr<T>()> m_creator;
    std::function<void(T*)>             m_recycler;
    std::function<void(T*)>             m_main_deleter{std::default_delete<T>()};
    std::function<void(T*)>             m_pool_deleter{[this](T* ptr) { release(ptr); }};
    lockfree_queue_based_on_pool<T*>    m_free_items;
};

} // namespace coro::detail
