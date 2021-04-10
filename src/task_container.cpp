#include "coro/task_container.hpp"
#include "coro/thread_pool.hpp"

#include <iostream>

namespace coro
{
// task_container::task_container(thread_pool& tp, const options opts)
//     : m_growth_factor(opts.growth_factor),
//       m_thread_pool(tp)
// {
//     m_tasks.resize(opts.reserve_size);
//     for (std::size_t i = 0; i < opts.reserve_size; ++i)
//     {
//         m_task_indexes.emplace_back(i);
//     }
//     m_free_pos = m_task_indexes.begin();
// }

// task_container::~task_container()
// {
//     // This will hang the current thread.. but if tasks are not complete thats also pretty bad.
//     while (!empty())
//     {
//         garbage_collect();
//     }
// }

// auto task_container::start(coro::task<void> user_task, garbage_collect_t cleanup) -> void
// {
//     m_size.fetch_add(1, std::memory_order::relaxed);

//     std::scoped_lock lk{m_mutex};

//     if (cleanup == garbage_collect_t::yes)
//     {
//         gc_internal();
//     }

//     // Only grow if completely full and attempting to add more.
//     if (m_free_pos == m_task_indexes.end())
//     {
//         m_free_pos = grow();
//     }

//     // Store the task inside a cleanup task for self deletion.
//     auto index     = *m_free_pos;
//     m_tasks[index] = make_cleanup_task(std::move(user_task), m_free_pos);

//     // Mark the current used slot as used.
//     std::advance(m_free_pos, 1);

//     // Start executing from the cleanup task to schedule the user's task onto the thread pool.
//     m_tasks[index].resume();
// }

// auto task_container::garbage_collect() -> std::size_t
// {
//     std::scoped_lock lk{m_mutex};
//     return gc_internal();
// }

// auto task_container::garbage_collect_and_yield_until_empty() -> coro::task<void>
// {
//     while (!empty())
//     {
//         garbage_collect();
//         co_await m_thread_pool.yield();
//     }
// }

// auto task_container::grow() -> task_position
// {
//     // Save an index at the current last item.
//     auto        last_pos = std::prev(m_task_indexes.end());
//     std::size_t new_size = m_tasks.size() * m_growth_factor;
//     for (std::size_t i = m_tasks.size(); i < new_size; ++i)
//     {
//         m_task_indexes.emplace_back(i);
//     }
//     m_tasks.resize(new_size);
//     // Set the free pos to the item just after the previous last item.
//     return std::next(last_pos);
// }

// auto task_container::gc_internal() -> std::size_t
// {
//     std::size_t deleted{0};
//     if (!m_tasks_to_delete.empty())
//     {
//         for (const auto& pos : m_tasks_to_delete)
//         {
//             // This doesn't actually 'delete' the task, it'll get overwritten when a
//             // new user task claims the free space.  It could be useful to actually
//             // delete the tasks so the coroutine stack frames are destroyed.  The advantage
//             // of letting a new task replace and old one though is that its a 1:1 exchange
//             // on delete and create, rather than a large pause here to delete all the
//             // completed tasks.

//             // Put the deleted position at the end of the free indexes list.
//             m_task_indexes.splice(m_task_indexes.end(), m_task_indexes, pos);
//         }
//         deleted = m_tasks_to_delete.size();
//         m_tasks_to_delete.clear();
//     }
//     return deleted;
// }

// auto task_container::make_cleanup_task(task<void> user_task, task_position pos) -> coro::task<void>
// {
//     // Immediately move the task onto the thread pool.
//     co_await m_thread_pool.schedule();

//     try
//     {
//         // Await the users task to complete.
//         co_await user_task;
//     }
//     catch (const std::exception& e)
//     {
//         // TODO: what would be a good way to report this to the user...?  Catching here is required
//         // since the co_await will unwrap the unhandled exception on the task.
//         // The user's task should ideally be wrapped in a catch all and handle it themselves, but
//         // that cannot be guaranteed.
//         std::cerr << "coro::task_container user_task had an unhandled exception e.what()= " << e.what() << "\n";
//     }
//     catch (...)
//     {
//         // don't crash if they throw something that isn't derived from std::exception
//         std::cerr << "coro::task_container user_task had unhandle exception, not derived from std::exception.\n";
//     }

//     std::scoped_lock lk{m_mutex};
//     m_tasks_to_delete.push_back(pos);
//     // This has to be done within scope lock to make sure this coroutine task completes before the
//     // task container object destructs -- if it was waiting on .empty() to become true.
//     m_size.fetch_sub(1, std::memory_order::relaxed);
//     co_return;
// }

} // namespace coro
