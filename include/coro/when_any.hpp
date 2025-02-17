#pragma once

// EMSCRIPTEN does not currently support std::jthread or std::stop_source|token.
#ifndef EMSCRIPTEN

    #include "coro/concepts/awaitable.hpp"
    #include "coro/detail/task_self_deleting.hpp"
    #include "coro/event.hpp"
    #include "coro/expected.hpp"
    #include "coro/mutex.hpp"
    #include "coro/task.hpp"

    #include <atomic>
    #include <cassert>
    #include <coroutine>
    #include <stop_token>
    #include <utility>
    #include <vector>

namespace coro
{

namespace detail
{

template<typename return_type, concepts::awaitable awaitable>
auto make_when_any_tuple_task(
    coro::mutex&                m,
    std::atomic<bool>&          return_value_set,
    coro::event&                notify,
    std::optional<return_type>& return_value,
    awaitable                   a) -> coro::task<void>
{
    auto result      = co_await static_cast<awaitable&&>(a);
    auto scoped_lock = co_await m.lock();
    if (return_value_set.load(std::memory_order::acquire) == false)
    {
        return_value_set.store(true, std::memory_order::release);
        return_value = std::move(result);
        notify.set();
    }

    co_return;
}

template<typename return_type, concepts::awaitable... awaitable_type>
[[nodiscard]] auto make_when_any_tuple_controller_task(
    coro::event& notify, std::optional<return_type>& return_value, awaitable_type... awaitables)
    -> coro::detail::task_self_deleting
{
    coro::mutex       m{};
    std::atomic<bool> return_value_set{false};

    co_await when_all(make_when_any_tuple_task(m, return_value_set, notify, return_value, std::move(awaitables))...);
    co_return;
}

template<concepts::awaitable awaitable, typename return_type>
static auto make_when_any_task(
    awaitable                   a,
    coro::mutex&                m,
    std::atomic<bool>&          return_value_set,
    coro::event&                notify,
    std::optional<return_type>& return_value) -> coro::task<void>
{
    auto result = co_await static_cast<awaitable&&>(a);
    co_await m.lock();
    // Its important to only touch return_value and notify once since their lifetimes will be destroyed
    // after being set ane notified the first time.
    if (return_value_set.load(std::memory_order::acquire) == false)
    {
        return_value_set.store(true, std::memory_order::release);
        return_value = std::move(result);
        notify.set();
    }

    co_return;
}

template<
    std::ranges::range  range_type,
    concepts::awaitable awaitable_type = std::ranges::range_value_t<range_type>,
    typename return_type               = typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type,
    typename return_type_base          = std::remove_reference_t<return_type>>
static auto make_when_any_controller_task(
    range_type awaitables, coro::event& notify, std::optional<return_type_base>& return_value)
    -> coro::detail::task_self_deleting
{
    // These must live for as long as the longest running when_any task since each task tries to see
    // if it was the first to complete. Only the very first task to complete will set the return_value
    // and notify.
    coro::mutex       m{};
    std::atomic<bool> return_value_set{false};

    // This detatched task will maintain the lifetime of all the when_any tasks.
    std::vector<coro::task<void>> tasks{};

    if constexpr (std::ranges::sized_range<range_type>)
    {
        tasks.reserve(std::size(awaitables));
    }

    for (auto&& a : awaitables)
    {
        tasks.emplace_back(make_when_any_task<awaitable_type, return_type_base>(
            std::move(a), m, return_value_set, notify, return_value));
    }

    co_await coro::when_all(std::move(tasks));
    co_return;
}

} // namespace detail

template<concepts::awaitable... awaitable_type>
[[nodiscard]] auto when_any(std::stop_source stop_source, awaitable_type... awaitables) -> coro::task<
    std::variant<std::remove_reference_t<typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>...>>
{
    using return_type = std::variant<
        std::remove_reference_t<typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>...>;

    std::optional<return_type> return_value{std::nullopt};
    coro::event                notify{};

    auto controller_task =
        detail::make_when_any_tuple_controller_task(notify, return_value, std::forward<awaitable_type>(awaitables)...);
    controller_task.handle().resume();

    co_await notify;
    stop_source.request_stop();
    co_return std::move(return_value.value());
}

template<concepts::awaitable... awaitable_type>
[[nodiscard]] auto when_any(awaitable_type... awaitables) -> coro::task<
    std::variant<std::remove_reference_t<typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>...>>
{
    using return_type = std::variant<
        std::remove_reference_t<typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>...>;

    std::optional<return_type> return_value{std::nullopt};
    coro::event                notify{};

    auto controller_task =
        detail::make_when_any_tuple_controller_task(notify, return_value, std::forward<awaitable_type>(awaitables)...);
    controller_task.handle().resume();

    co_await notify;
    co_return std::move(return_value.value());
}

template<
    std::ranges::range  range_type,
    concepts::awaitable awaitable_type = std::ranges::range_value_t<range_type>,
    typename return_type               = typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type,
    typename return_type_base          = std::remove_reference_t<return_type>>
[[nodiscard]] auto when_any(std::stop_source stop_source, range_type awaitables) -> coro::task<return_type_base>
{
    // Using an std::optional to prevent the need to default construct the type on the stack.
    std::optional<return_type_base> return_value{std::nullopt};
    coro::event                     notify{};

    auto controller_task =
        detail::make_when_any_controller_task(std::forward<range_type>(awaitables), notify, return_value);
    controller_task.handle().resume();

    co_await notify;
    stop_source.request_stop();
    co_return std::move(return_value.value());
}

template<
    std::ranges::range  range_type,
    concepts::awaitable awaitable_type = std::ranges::range_value_t<range_type>,
    typename return_type               = typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type,
    typename return_type_base          = std::remove_reference_t<return_type>>
[[nodiscard]] auto when_any(range_type awaitables) -> coro::task<return_type_base>
{
    std::optional<return_type_base> return_value{std::nullopt};
    coro::event                     notify{};

    auto controller_task =
        detail::make_when_any_controller_task(std::forward<range_type>(awaitables), notify, return_value);
    controller_task.handle().resume();

    co_await notify;
    co_return std::move(return_value.value());
}

} // namespace coro

#endif // EMSCRIPTEN
