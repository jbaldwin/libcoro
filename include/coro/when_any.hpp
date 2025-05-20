#pragma once

// EMSCRIPTEN does not currently support std::jthread or std::stop_source|token.
#ifndef EMSCRIPTEN

    #include "coro/concepts/awaitable.hpp"
    #include "coro/detail/task_self_deleting.hpp"
    #include "coro/event.hpp"
    #include "coro/expected.hpp"
    #include "coro/mutex.hpp"
    #include "coro/task.hpp"
    #include "coro/when_all.hpp"

    #include <atomic>
    #include <cassert>
    #include <coroutine>
    #include <stop_token>
    #include <optional>
    #include <utility>
    #include <vector>

namespace coro
{

namespace detail
{

template<typename return_type, concepts::awaitable awaitable>
auto make_when_any_tuple_task(
    std::atomic<bool>& first_completed, coro::event& notify, std::optional<return_type>& return_value, awaitable a)
    -> coro::task<void>
{
    auto expected = false;
    auto result   = co_await static_cast<awaitable&&>(a);
    if (first_completed.compare_exchange_strong(expected, true, std::memory_order::acq_rel, std::memory_order::relaxed))
    {
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
    std::atomic<bool> first_completed{false};
    co_await when_all(make_when_any_tuple_task(first_completed, notify, return_value, std::move(awaitables))...);
    co_return;
}

template<concepts::awaitable awaitable>
static auto make_when_any_task_return_void(awaitable a, coro::event& notify) -> coro::task<void>
{
    co_await static_cast<awaitable&&>(a);
    notify.set(); // This will trigger the controller task to wake up exactly once.
    co_return;
}

template<concepts::awaitable awaitable, typename return_type>
static auto make_when_any_task(
    awaitable a, std::atomic<bool>& first_completed, coro::event& notify, std::optional<return_type>& return_value)
    -> coro::task<void>
{
    auto expected = false;
    auto result   = co_await static_cast<awaitable&&>(a);
    // Its important to only touch return_value and notify once since their lifetimes will be destroyed
    // after being set and notified the first time.
    if (first_completed.compare_exchange_strong(expected, true, std::memory_order::acq_rel, std::memory_order::relaxed))
    {
        return_value = std::move(result);
        notify.set();
    }

    co_return;
}

template<std::ranges::range range_type, concepts::awaitable awaitable_type = std::ranges::range_value_t<range_type>>
static auto make_when_any_controller_task_return_void(range_type awaitables, coro::event& notify)
    -> coro::detail::task_self_deleting
{
    std::vector<coro::task<void>> tasks{};

    if constexpr (std::ranges::sized_range<range_type>)
    {
        tasks.reserve(std::size(awaitables));
    }

    for (auto&& a : awaitables)
    {
        tasks.emplace_back(make_when_any_task_return_void<awaitable_type>(std::move(a), notify));
    }

    co_await coro::when_all(std::move(tasks));
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
    // This must live for as long as the longest running when_any task since each task tries to see
    // if it was the first to complete. Only the very first task to complete will set the return_value
    // and notify.
    std::atomic<bool> first_completed{false};

    // This detatched task will maintain the lifetime of all the when_any tasks.
    std::vector<coro::task<void>> tasks{};

    if constexpr (std::ranges::sized_range<range_type>)
    {
        tasks.reserve(std::size(awaitables));
    }

    for (auto&& a : awaitables)
    {
        tasks.emplace_back(
            make_when_any_task<awaitable_type, return_type_base>(std::move(a), first_completed, notify, return_value));
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

    coro::event                notify{};
    std::optional<return_type> return_value{std::nullopt};
    auto                       controller_task =
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

    coro::event                notify{};
    std::optional<return_type> return_value{std::nullopt};
    auto                       controller_task =
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
    coro::event notify{};

    if constexpr (std::is_void_v<return_type_base>)
    {
        auto controller_task =
            detail::make_when_any_controller_task_return_void(std::forward<range_type>(awaitables), notify);
        controller_task.handle().resume();

        co_await notify;
        stop_source.request_stop();
        co_return;
    }
    else
    {
        // Using an std::optional to prevent the need to default construct the type on the stack.
        std::optional<return_type_base> return_value{std::nullopt};

        auto controller_task =
            detail::make_when_any_controller_task(std::forward<range_type>(awaitables), notify, return_value);
        controller_task.handle().resume();

        co_await notify;
        stop_source.request_stop();

        co_return std::move(return_value.value());
    }
}

template<
    std::ranges::range  range_type,
    concepts::awaitable awaitable_type = std::ranges::range_value_t<range_type>,
    typename return_type               = typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type,
    typename return_type_base          = std::remove_reference_t<return_type>>
[[nodiscard]] auto when_any(range_type awaitables) -> coro::task<return_type_base>
{
    coro::event notify{};

    if constexpr (std::is_void_v<return_type_base>)
    {
        auto controller_task =
            detail::make_when_any_controller_task_return_void(std::forward<range_type>(awaitables), notify);
        controller_task.handle().resume();

        co_await notify;
        co_return;
    }
    else
    {
        std::optional<return_type_base> return_value{std::nullopt};

        auto controller_task =
            detail::make_when_any_controller_task(std::forward<range_type>(awaitables), notify, return_value);
        controller_task.handle().resume();

        co_await notify;

        co_return std::move(return_value.value());
    }
}

} // namespace coro

#endif // EMSCRIPTEN
