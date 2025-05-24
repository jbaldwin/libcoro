#pragma once

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/io_scheduler.hpp"
#else
    #include "coro/thread_pool.hpp"
#endif

namespace coro::default_executor
{

/**
 * Set up default coro::thread_pool::options before constructing a single instance of coro::thread_pool in
 * coro::default_executor::executor()
 * @param thread_pool_options thread_pool options
 */
void set_executor_options(thread_pool::options thread_pool_options);

/**
 * Get default coro::thread_pool
 */
std::shared_ptr<thread_pool> executor();

#ifdef LIBCORO_FEATURE_NETWORKING
/**
 * Set up default coro::io_scheduler::options before constructing a single instance of coro::io_scheduler in
 * coro::default_executor::io_executor()
 * @param io_scheduler_options io_scheduler options
 */
void set_io_executor_options(io_scheduler::options io_scheduler_options);

/**
 * Get default coro::io_scheduler
 */
std::shared_ptr<io_scheduler> io_executor();
#endif

/**
 * Get the perfect default executor
 *
 * This executor is ideal as a default argument in a library,
 * in a place where thread_pool functionality is sufficient,
 * but you don't want to have two executor instances per application for the same thing,
 * one thread_pool and one io_scheduler.
 */
inline auto perfect()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return io_executor();
#else
    return executor();
#endif
}

} // namespace coro::default_executor
