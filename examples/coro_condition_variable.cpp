#include <coro/coro.hpp>
#include <iostream>
#include <queue>
#include <vector>

using namespace std::chrono_literals;

// Coroutine thread-safe queue
template<typename T>
class TSQueue
{
public:
    TSQueue() = default;

    explicit TSQueue(std::stop_source& stop_source)
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
    [[nodiscard]] auto pop() -> coro::task<coro::expected<T, std::stop_token>>
    {
        // acquire lock
        auto lock = co_await m_mutex.lock();

        // wait until queue is not empty or stop requested
        co_await m_cond.wait(lock, [this]() { return !m_queue.empty() || m_stop.stop_requested(); });

        if (m_stop.stop_requested())
        {
            co_return coro::unexpected<std::stop_token>(m_stop);
        }

        assert(!m_queue.empty());

        // retrieve item
        T item = m_queue.front();
        m_queue.pop();

        co_return coro::expected<T, std::stop_token>{item};
    }

    [[nodiscard]] auto empty() -> coro::task<bool>
    {
        // acquire lock
        auto lock = co_await m_mutex.lock();

        co_return m_queue.empty();
    }

private:
    std::stop_token m_stop;

    std::stop_callback<std::function<void()>> m_stop_callback;

    // Underlying queue
    std::queue<T> m_queue;

    // mutex for coroutine synchronization
    coro::mutex m_mutex;

    // Condition variable for signaling
    coro::condition_variable m_cond;
};

struct Params
{
    std::stop_source stop_source;
    int              max_value{};
    std::atomic_int  next_value{};
    TSQueue<int>     queue{stop_source};
    std::vector<int> output;
};

int main()
{
    auto                          default_scheduler = coro::default_executor::executor();
    std::vector<coro::task<void>> tasks{};
    auto                          params = std::make_shared<Params>();
    params->max_value                    = 20;

    // These tasks will produce values ​​and put them into a queue
    auto make_producer_task = [](auto scheduler, std::shared_ptr<Params> p) -> coro::task<void>
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
    auto make_consumer_task = [](auto scheduler, std::shared_ptr<Params> p) -> coro::task<void>
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

    auto make_director_task = [](auto scheduler, std::shared_ptr<Params> p) -> coro::task<void>
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
        tasks.emplace_back(make_consumer_task(default_scheduler, params));
    }

    for (int i = 0; i < 3; ++i)
    {
        tasks.emplace_back(make_producer_task(default_scheduler, params));
    }

    tasks.emplace_back(make_director_task(default_scheduler, params));

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all(std::move(tasks)));
    for (auto& v : params->output)
    {
        std::cout << v << std::endl;
    }

    std::cout << "finished" << std::endl;
    return 0;
}
