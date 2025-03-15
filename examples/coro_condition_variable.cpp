#include <coro/coro.hpp>
#include <iostream>
#include <queue>
#include <vector>

#ifdef EMSCRIPTEN
    #include <set>
#endif

using namespace std::chrono_literals;

namespace coro
{

#ifdef EMSCRIPTEN
class stop_token;
class stop_source;

template<typename Callback>
class stop_callback;

class istop_callback
{
public:
    virtual void operator()() = 0;
};

class stop_token
{
public:
    explicit stop_token(std::weak_ptr<std::atomic_bool> stop_requested) : m_stop_requested(stop_requested) {}

    bool stop_requested() const
    {
        if (auto sr = m_stop_requested.lock())
        {
            return *sr;
        }
        return true;
    }

private:
    friend class stop_source;

    template<typename Callback>
    friend class stop_callback;

    std::weak_ptr<std::atomic_bool>            m_stop_requested;
    std::shared_ptr<std::set<istop_callback*>> m_callbacks = std::make_shared<std::set<istop_callback*>>();

    void request_stop()
    {
        for (auto cb : *m_callbacks)
        {
            (*cb)();
        }
    }
};

template<typename Callback>
class stop_callback : istop_callback
{
public:
    stop_callback(stop_token& st, Callback cb) : m_stop_token(st), m_callback(cb)
    {
        m_stop_token.m_callbacks->insert(this);
    }

    ~stop_callback() { m_stop_token.m_callbacks->erase(this); }

private:
    friend class stop_token;

    stop_token& m_stop_token;
    Callback    m_callback;

    void operator()() { m_callback(); }
};

class stop_source
{
public:
    stop_token get_token()
    {
        stop_token result{m_stop_requested};
        m_tokens.push_back(result);
        return result;
    }

    void request_stop()
    {
        bool expected{};
        if (!m_stop_requested->compare_exchange_strong(
                expected, true, std::memory_order::release, std::memory_order::acquire))
            return;

        for (auto& token : m_tokens)
        {
            token.request_stop();
        }
    }

private:
    std::shared_ptr<std::atomic_bool> m_stop_requested = std::make_shared<std::atomic_bool>();
    std::vector<stop_token>           m_tokens;
};
#else
using stop_source = std::stop_source;
using stop_token  = std::stop_token;

template<class Callback>
using stop_callback = std::stop_callback<Callback>;
#endif
} // namespace coro

// Coroutine thread-safe queue
template<typename T>
class TSQueue
{
public:
    TSQueue() = default;

    explicit TSQueue(coro::stop_source& stop_source)
        : m_stop(stop_source.get_token()),
          m_stop_callback(m_stop, [this] { m_cond.notify_all(); })
    {
    }

    // Pushes an element to the queue
    [[nodiscard]] auto push(T item) -> coro::task<void>
    {
        // Acquire lock
        auto lock = co_await m_mutex.lock();

        // Add item
        m_queue.push(item);

        // Notify one coroutine that
        // is waiting
        m_cond.notify_one();
    }

    // Pops an element off the queue
    [[nodiscard]] auto pop() -> coro::task<coro::expected<T, coro::stop_token>>
    {
        // acquire lock
        auto lock = co_await m_mutex.lock();

        // wait until queue is not empty or stop requested
        co_await m_cond.wait(lock, [this]() { return !m_queue.empty() || m_stop.stop_requested(); });

        if (m_stop.stop_requested())
        {
            co_return coro::unexpected<coro::stop_token>(m_stop);
        }

        assert(!m_queue.empty());

        // retrieve item
        T item = m_queue.front();
        m_queue.pop();

        co_return coro::expected<T, coro::stop_token>{item};
    }

    [[nodiscard]] auto empty() -> coro::task<bool>
    {
        // acquire lock
        auto lock = co_await m_mutex.lock();

        co_return m_queue.empty();
    }

private:
    coro::stop_token m_stop;

    coro::stop_callback<std::function<void()>> m_stop_callback;

    // Underlying queue
    std::queue<T> m_queue;

    // mutex for coroutine synchronization
    coro::mutex m_mutex;

    // Condition variable for signaling
    coro::condition_variable m_cond;
};

struct Params
{
    coro::stop_source stop_source;
    int               max_value{};
    std::atomic_int   next_value{};
    TSQueue<int>      queue{stop_source};
    std::vector<int>  output;
};

int main()
{
    auto*                         scheduler = coro::facade::instance();
    std::vector<coro::task<void>> tasks{};
    auto                          params = std::make_shared<Params>();
    params->max_value                    = 20;

    // These tasks will produce values ​​and put them into a queue
    auto make_producer_task = [](auto* scheduler, std::shared_ptr<Params> p) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        auto v = p->next_value.fetch_add(1);
        while (v < p->max_value)
        {
            co_await p->queue.push(v);
            v = p->next_value.fetch_add(1);

            co_await scheduler->yield();
        }

        co_return;
    };

    // These tasks will consume values ​​from the queue, and wait for new data if the queue is empty
    auto make_consumer_task = [](auto* scheduler, std::shared_ptr<Params> p) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        while (true)
        {
            auto v = co_await p->queue.pop();

            if (!v.has_value())
            {
                co_return;
            }

            p->output.push_back(v.value());

            co_await scheduler->yield();
        }

        co_return;
    };

    auto make_director_task = [](auto* scheduler, std::shared_ptr<Params> p) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        while (true)
        {
            if ((p->next_value.load(std::memory_order::acquire) >= p->max_value) && (co_await p->queue.empty()))
            {
                break;
            }

            std::this_thread::sleep_for(16ms);
            co_await scheduler->yield();
        }

        p->stop_source.request_stop();
        co_return;
    };

    for (int i = 0; i < 3; ++i)
    {
        tasks.emplace_back(make_consumer_task(scheduler, params));
    }

    for (int i = 0; i < 3; ++i)
    {
        tasks.emplace_back(make_producer_task(scheduler, params));
    }

    tasks.emplace_back(make_director_task(scheduler, params));

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all(std::move(tasks)));
    for (auto& v : params->output)
    {
        std::cout << v << std::endl;
    }

    std::cout << "finished" << std::endl;
    return 0;
}
