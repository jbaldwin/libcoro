#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace coro
{

namespace detail::lock_free_queue
{

template<typename T>
struct node
{
    using value_type = T;

    T                     data{};
    std::atomic<node<T>*> next{nullptr};
    std::atomic<int32_t>  ref_count{0};
    std::atomic<bool>     is_removed{false};
    std::atomic<bool>     is_cleaned{true};
    bool                  is_dummy{false};

    node() = default;
    explicit node(const T& data) : data(data), is_cleaned(false) {}

    void clean()
    {
        assert(ref_count.load(std::memory_order::acquire) >= 0);

        data = {};
        next.store(nullptr, std::memory_order::release);
        is_removed.store(false, std::memory_order::release);
        is_cleaned.store(true, std::memory_order::release);
    }
};

template<typename T, class Dispose>
class guard_ptr
{
    node<T>* m_node_ptr;
    Dispose  m_dispose;
    bool     m_is_removed{};

    void decrement()
    {
        assert(m_node_ptr->ref_count.load(std::memory_order::acquire) >= 1);

        if (m_node_ptr->ref_count.fetch_sub(1, std::memory_order::acq_rel) < 2)
        {
            if (m_node_ptr->is_removed.load(std::memory_order::acquire) &&
                m_node_ptr->ref_count.load(std::memory_order::acquire) <= 0)
            {
                m_dispose(m_node_ptr);
                m_node_ptr = nullptr;
            }
        }

        m_node_ptr = nullptr;
    }

public:
    explicit guard_ptr(std::atomic<node<T>*>& ref, Dispose dispose) : m_dispose(dispose)
    {
        using namespace std::chrono_literals;

        do
        {
            m_node_ptr = ref.load(std::memory_order::acquire);
            if (!m_node_ptr)
            {
                return;
            }
            if (m_node_ptr->is_removed.load(std::memory_order::acquire))
            {
                m_is_removed = true;
                m_node_ptr   = nullptr;
                return;
            }
            node<T>* current = ref.load(std::memory_order::acquire);
            if (m_node_ptr == current)
            {
                break;
            }
        } while (true);
        m_node_ptr->ref_count.fetch_add(1, std::memory_order::acq_rel);
    }

    void remove()
    {
        if (!m_node_ptr)
        {
            return;
        }

        m_is_removed = true;
        m_node_ptr->is_removed.store(true, std::memory_order::release);
        decrement();
    }

    bool is_removed() const { return m_is_removed; }

    node<T>* value() { return m_node_ptr; }

    ~guard_ptr()
    {
        if (m_node_ptr)
        {
            decrement();
        }
    }
};

} // namespace detail::lock_free_queue

namespace concepts
{

// clang-format off

template<typename allocator_type, typename item_type = allocator_type::item_type, typename node_type = allocator_type::node_type>
concept lockfree_queue_allocator = requires(allocator_type a, node_type* released_node, node_type* data_node, item_type item)
{
    { a.allocate(item) }-> std::same_as<node_type*>;
    { a.return_value(released_node, data_node) } -> std::same_as<item_type>;
    { a.deallocate(released_node) } -> std::same_as<void>;
};

// clang-format on

} // namespace concepts

namespace detail
{

namespace lock_free_queue
{

template<typename T, typename N = node<T>, typename std_allocator_type = std::allocator<N>>
class std_allocator_adapter
{
public:
    using item_type = T;
    using node_type = N;

    constexpr node_type* allocate(const item_type& item)
    {
        auto result = alloc.allocate(1);
        std::construct_at(result, item);
        return result;
    }
    constexpr item_type return_value(node_type* released_node, node_type* data_node)
    {
        (void)released_node;
        return data_node->data;
    }
    constexpr void deallocate(node_type* released_node)
    {
        std::destroy_at(released_node);
        alloc.deallocate(released_node, 1);
    }

private:
    std_allocator_type alloc;
};

} // namespace lock_free_queue

// Michael-Scott Queue
template<typename T, coro::concepts::lockfree_queue_allocator<T> Alloc = lock_free_queue::std_allocator_adapter<T>>
class lockfree_queue
{
protected:
    using node_type  = Alloc::node_type;
    using guard_type = lock_free_queue::guard_ptr<typename node_type::value_type, std::function<void(node_type*)>>;
    Alloc                   m_alloc;
    node_type               m_dummy;
    std::atomic<node_type*> m_head;
    std::atomic<node_type*> m_tail;

    void dispose_node(node_type* ptr)
    {
        if (ptr != &m_dummy)
        {
            m_alloc.deallocate(ptr);
        }
    }

public:
    lockfree_queue() : m_head(&m_dummy), m_tail(&m_dummy) { m_dummy.is_dummy = true; }
    explicit lockfree_queue(Alloc allocator) : m_alloc(allocator), m_head(&m_dummy), m_tail(&m_dummy)
    {
        m_dummy.is_dummy = true;
    }

    ~lockfree_queue()
    {
        clear();

        auto head_node = m_head.load(std::memory_order::relaxed);

        assert(head_node != nullptr);
        assert(head_node == m_tail.load(std::memory_order::relaxed));

        m_head.store(nullptr, std::memory_order::relaxed);
        m_tail.store(nullptr, std::memory_order::relaxed);

        dispose_node(head_node);
    }

    void push(const T& value)
    {
        using namespace std::chrono_literals;

        auto new_node = m_alloc.allocate(value);
        assert(new_node->next.load() == nullptr);
        while (true)
        {
            guard_type tail_guard(m_tail, [this](node_type* n) { dispose_node(n); });
            if (tail_guard.is_removed())
            {
                continue;
            }
            auto tail_node = tail_guard.value();
            assert(tail_node != new_node);
            auto next_node = tail_node->next.load(std::memory_order::acquire);

            if (next_node != nullptr)
            {
                assert(tail_node != next_node);

                // Tail is misplaced, advance it
                m_tail.compare_exchange_weak(
                    tail_node, next_node, std::memory_order::release, std::memory_order::relaxed);
                continue;
            }

            node_type* expected = nullptr;
            if (tail_node->next.compare_exchange_strong(
                    expected, new_node, std::memory_order::release, std::memory_order::relaxed))
            {
                assert(tail_node != new_node);
                m_tail.compare_exchange_strong(
                    tail_node, new_node, std::memory_order::release, std::memory_order::relaxed);
                return;
            }

            std::this_thread::sleep_for(1us);
        }
    }

    std::optional<T> pop()
    {
        using namespace std::chrono_literals;

        while (true)
        {
            guard_type head_guard(m_head, [this](node_type* n) { dispose_node(n); });
            if (head_guard.is_removed())
            {
                continue;
            }
            auto       head_node = head_guard.value();
            guard_type next_guard(head_node->next, [this](node_type* n) { dispose_node(n); });
            if (next_guard.is_removed())
            {
                continue;
            }
            auto next_node = next_guard.value();

            if (m_head.load(std::memory_order::acquire) != head_node)
            {
                continue;
            }

            if (next_node == nullptr)
            {
                return {}; // Queue is empty
            }

            auto tail_node = m_tail.load(std::memory_order::acquire);

            if (head_node == tail_node)
            {
                assert(tail_node != next_node);

                // It is needed to help push()
                m_tail.compare_exchange_strong(
                    tail_node, next_node, std::memory_order::release, std::memory_order::relaxed);
                continue;
            }

            assert(head_node != next_node);

            if (m_head.compare_exchange_strong(
                    head_node, next_node, std::memory_order::acquire, std::memory_order::relaxed))
            {
                std::optional<T> result = m_alloc.return_value(head_node, next_node);
                if (head_node != &m_dummy)
                    head_guard.remove();
                return result;
            }

            std::this_thread::sleep_for(1us);
        }
    }

    bool empty() const
    {
        guard_type head_guard(m_head, [this](node_type* n) { dispose_node(n); });
        auto       head_node = head_guard.value();
        return head_node->next.load(std::memory_order::relaxed) == nullptr;
    }

    void clear()
    {
        while (pop()) {}
    }
};

template<typename T>
class lockfree_queue_based_on_pool
{
public:
    void push(const T& value) { m_queue_used.push(value); }

    std::optional<T> pop() { return m_queue_used.pop(); }

    bool empty() const { return m_queue_used.empty(); }

    void clear()
    {
        while (pop()) {}
    }

    ~lockfree_queue_based_on_pool()
    {
        clear();
        m_shutdown.store(true, std::memory_order::release);
    }

private:
    std::atomic<bool> m_shutdown{false};

    class pool_allocator
    {
    public:
        using node_type = lock_free_queue::node<T>;
        using item_type = T;

        explicit pool_allocator(lockfree_queue_based_on_pool& queue) : m_queue(queue) {}

        constexpr node_type* allocate(const item_type& item)
        {
            using namespace std::chrono_literals;

            while (true)
            {
                auto node = m_queue.m_queue_free.pop();
                if (node)
                {
                    node_type* result = node.value();
                    if (result->is_dummy)
                        continue;

                    bool is_shutdown = false;
                    while (!(is_shutdown = m_queue.m_shutdown.load(std::memory_order::acquire)) &&
                           !result->is_cleaned.load(std::memory_order::acquire))
                    {
                        std::this_thread::sleep_for(1us);
                    }

                    if (is_shutdown)
                    {
                        assert("attempt to use a queue at the moment of its destruction");
                        throw std::bad_alloc();
                    }

                    result->data = item;
                    return result;
                }
                return m_queue.allocate(item);
            }
        }
        constexpr item_type return_value(node_type* released_node, node_type* data_node)
        {
            (void)released_node;
            return data_node->data;
        }
        constexpr void deallocate(node_type* released_node)
        {
            if (m_queue.m_shutdown.load(std::memory_order::acquire))
            {
                m_queue.deallocate(released_node);
                return;
            }

            released_node->clean();
            m_queue.m_queue_free.push(released_node);
        }

    private:
        lockfree_queue_based_on_pool& m_queue;
    };

    class free_allocator
    {
    public:
        using node_type = lock_free_queue::node<T>;
        using item_type = node_type*;

        explicit free_allocator(lockfree_queue_based_on_pool& queue) : m_queue(queue) {}

        constexpr node_type* allocate(const item_type& item)
        {
            assert(item);
            item->is_cleaned.store(false, std::memory_order::release);
            return item;
        }
        constexpr item_type return_value(node_type* released_node, node_type* data_node)
        {
            (void)data_node;
            return released_node;
        }
        constexpr void deallocate(node_type* released_node)
        {
            if (m_queue.m_shutdown.load(std::memory_order::acquire))
            {
                m_queue.deallocate(released_node);
                return;
            }

            released_node->clean();
        }

    private:
        lockfree_queue_based_on_pool& m_queue;
    };

    using node_type = lock_free_queue::node<T>;

    lockfree_queue<node_type*, free_allocator> m_queue_free{free_allocator(*this)};
    lockfree_queue<T, pool_allocator>          m_queue_used{pool_allocator(*this)};

    constexpr node_type* allocate(const T& item)
    {
        auto result = new node_type(item);
        return result;
    }
    constexpr void deallocate(node_type* released_node) { delete released_node; }
};

} // namespace detail
} // namespace coro
