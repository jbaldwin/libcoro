#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace coro
{

/**
 * This concept declares a type that is required to meet the c++20 coroutine operator co_await()
 * retun type.  It requires the following three member functions:
 *      await_ready() -> bool
 *      await_suspend(std::coroutine_handle<>) -> void|bool|std::coroutine_handle<>
 *      await_resume() -> decltype(auto)
 *          Where the return type on await_resume is the requested return of the awaitable.
 */
template<typename T>
concept awaiter_type = requires(T t, std::coroutine_handle<> c)
{
    { t.await_ready() } -> std::same_as<bool>;
    std::same_as<decltype(t.await_suspend(c)), void> ||
        std::same_as<decltype(t.await_suspend(c)), bool> ||
        std::same_as<decltype(t.await_suspend(c)), std::coroutine_handle<>>;
    { t.await_resume() };
};

/**
 * This concept declares a type that can be operator co_await()'ed and returns an awaiter_type.
 */
template<typename T>
concept awaitable_type = requires(T t)
{
    // operator co_await()
    { t.operator co_await() } -> awaiter_type;
};

template<awaitable_type awaitable, typename = void>
struct awaitable_traits
{
};

template<typename T>
static auto get_awaiter(T&& value)
{
    return static_cast<T&&>(value).operator co_await();
}

template<awaitable_type awaitable>
struct awaitable_traits<awaitable>
{
    using awaiter_t = decltype(get_awaiter(std::declval<awaitable>()));
    using awaiter_return_t = decltype(std::declval<awaiter_t>().await_resume());
    // using awaiter_return_decay_t = std::decay_t<decltype(std::declval<awaiter_t>().await_resume())>;
};

} // namespace coro
