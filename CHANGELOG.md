# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.17.0-alpha] - 2026-11-15

### Added
- `io_status::kind::unknown` [#444] [@jbaldwin]
- This changelog [#465] [@PyXiion]
- Release workflow that publishes a GitHub release with notes from this changelog when a `vX.Y.Z` tag is pushed [#465] [@PyXiion]

### Changed
- `coro::scheduler` no longer takes a lock when scheduling or resuming tasks; they are enqueued on an atomic waiter list and run in order. Directly resumed `std::coroutine_handle<>`s allocate a `schedule_operation`, `co_await`ed tasks do not. Operations with timeouts via `poll_info` still use a global lock [#444] [@jbaldwin]
- `-Wall`, `-Wextra` and `/W4` are now private to the project instead of being propagated to consumers [#457] [@daantimmer]
- Removed the `-pipe` compile flag [#456] [@jbaldwin]
- **Breaking:** `coro::thread_pool` now uses per-worker LIFO queues with work stealing instead of one shared FIFO queue, so recursive fork-join workloads run depth first and use far less memory (nqueens peak RSS 5.66 GiB -> 11 MiB). External submissions, `resume()`, `yield()` and `spawn_*` still use the shared FIFO queue, which workers check periodically to avoid starvation [#460] [@PyXiion]
- **Breaking:** `schedule()` called from a pool worker now runs local work in LIFO order; use `yield()` to give other queued tasks a turn [#460] [@PyXiion]
- **Breaking:** `coro::scheduler::process_events()` now returns `bool` [#460] [@PyXiion]
- `thread_pool` shutdown rejects new publications and drains accepted resumptions. Tasks suspended on external events must be joined before shutting down the pool [#460] [@PyXiion]

### Removed
- **Breaking:** `thread_pool::size()`, `thread_pool::queue_size()` and `scheduler::size()`. Use `empty()` where available [#460] [@PyXiion]

### Fixed
- `coro::task_group` no longer blocks forever when awaited without any tasks having been started [#451] [@ripose-jp]
- Android CI uses `android-actions/setup-android@v4` [#463] [@jbaldwin]


[#444]: https://github.com/jbaldwin/libcoro/pull/444
[#451]: https://github.com/jbaldwin/libcoro/pull/451
[#456]: https://github.com/jbaldwin/libcoro/pull/456
[#457]: https://github.com/jbaldwin/libcoro/pull/457
[#460]: https://github.com/jbaldwin/libcoro/pull/460
[#463]: https://github.com/jbaldwin/libcoro/pull/463
[#465]: https://github.com/jbaldwin/libcoro/pull/465
[@PyXiion]: https://github.com/PyXiion
[@jbaldwin]: https://github.com/jbaldwin
[@ripose-jp]: https://github.com/ripose-jp
[@daantimmer]: https://github.com/daantimmer