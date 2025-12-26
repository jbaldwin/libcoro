#pragma once

#include <utility>

namespace coro
{

namespace detail
{

template<typename functor_type, typename... args_types>
auto make_invoker_task(functor_type functor, args_types&&... args) -> decltype(functor(std::forward<args_types>(args)...))
{
    auto user_task = functor(std::forward<args_types>(args)...);
    co_return co_await user_task;
}

} // namespace detail

/**
 * @brief Invokes the given functor as a coroutine. This is useful if you want
 *        to use lambda captures and need them to be captured onto a stable
 *        coroutine frame.
 *
 * @code {.cpp}
 * int a = 1;
 * int b = 2;
 * auto make_task = [&a](int c) -> coro::task<int>
 * {
 *     co_await task_that_suspends();
 *     return a + b;
 * }
 *
 * // This is undefined since &a capture will be dangling.
 * co_await make_task(b);
 *
 * // This is well formed since it will correctly capture on the invoke
 * // coroutine frame.
 * co_await coro::invoke(make_task, b);
 * @endcode
 *
 *
 * @tparam functor_type Functor type.
 * @tparam args_types Argument types.
 * @param functor The functor to invoke as a coroutine.
 * @param args Arguments for functor_type
 * @return functor_type -> return_type
 */
template<typename functor_type, typename... args_types>
auto invoke(functor_type functor, args_types&&... args) -> decltype(auto)
{
    auto invoker_task = detail::make_invoker_task(std::forward<functor_type>(functor), std::forward<args_types>(args)...);
    invoker_task.resume();
    return invoker_task;

    //co_return co_await functor(std::forward<args_types>(args)...);
}

} // namespace coro
