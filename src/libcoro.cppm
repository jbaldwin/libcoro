module;
#include "coro/coro.hpp"

export module libcoro;

export namespace coro
{
// condition_variable.hpp
using coro::condition_variable;

// event.hpp
using coro::event;
using coro::resume_order_policy;

// generator.hpp
using coro::generator;

// invoke.hpp
using coro::invoke;

// latch.hpp
using coro::latch;

// mutex.hpp
using coro::mutex;
using coro::scoped_lock;

// queue.hpp
using coro::queue;

// ring_buffer.hpp
namespace ring_buffer_result
{
using coro::ring_buffer_result::consume;
using coro::ring_buffer_result::produce;
} // namespace ring_buffer_result
using coro::ring_buffer;

// default_executor.hpp
namespace default_executor
{
using coro::default_executor::perfect;

using coro::default_executor::executor;
using coro::default_executor::set_executor_options;
#ifdef LIBCORO_FEATURE_NETWORKING
using coro::default_executor::io_executor;
using coro::default_executor::set_io_executor_options;
#endif
} // namespace default_executor

// scheduler.hpp
using coro::scheduler;
using coro::timeout_status;

// semaphore.hpp
using coro::semaphore;
using coro::semaphore_acquire_result;

// shared_mutex.hpp
using coro::shared_mutex;

// sync_wait.hpp
using coro::sync_wait;

// task.hpp
using coro::task;

// task_group.hpp
using coro::task_group;

// thread_pool.hpp
using coro::thread_pool;

// when_all.hpp
using coro::when_all;

// when_any.hpp
using coro::when_any;

#ifdef LIBCORO_FEATURE_NETWORKING
namespace net
{
// net/
// connect.hpp
using net::connect_status;

// hostname.hpp
using net::hostname;

// io_status.hpp
using net::io_status;

// ip_address.hpp
using net::ip_address;

// socket.hpp
using net::socket;

// socket_address.hpp
using net::socket_address;

// net/dns/resolver.hpp
namespace dns
{
using dns::resolver;
using dns::result;
} // namespace dns

// net/tcp/*
namespace tcp
{
using tcp::client;
using tcp::server;
} // namespace tcp

// net/udp/peer.hpp
namespace udp
{
using udp::peer;
}

// net/tls/*
namespace tls
{
using tls::client;
using tls::server;

// connection_status.hpp
using tls::connection_status;
// context.hpp
using tls::context;
using tls::tls_file_type;
using tls::verify_peer_t;
// recv_status.hpp / send_status.hpp
using tls::recv_status;
using tls::send_status;
} // namespace tls
} // namespace net
#endif
} // namespace coro