#pragma once

#include "coro/concepts/awaitable.hpp"
#include "coro/concepts/buffer.hpp"
#include "coro/concepts/executor.hpp"
#include "coro/concepts/promise.hpp"
#include "coro/concepts/range_of.hpp"

#ifdef LIBCORO_FEATURE_THREADING
    #include "coro/io_scheduler.hpp"
    #include "coro/poll.hpp"
#endif

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/net/connect.hpp"
    #include "coro/net/dns_resolver.hpp"
    #include "coro/net/hostname.hpp"
    #include "coro/net/ip_address.hpp"
    #include "coro/net/recv_status.hpp"
    #include "coro/net/send_status.hpp"
    #include "coro/net/socket.hpp"
    #ifdef LIBCORO_FEATURE_SSL
        #include "coro/net/ssl_context.hpp"
    #endif
    #include "coro/net/tcp_client.hpp"
    #include "coro/net/tcp_server.hpp"
    #include "coro/net/udp_peer.hpp"
#endif

#include "coro/event.hpp"
#include "coro/generator.hpp"
#include "coro/latch.hpp"
#include "coro/mutex.hpp"
#include "coro/ring_buffer.hpp"
#include "coro/semaphore.hpp"
#include "coro/shared_mutex.hpp"
#include "coro/sync_wait.hpp"
#include "coro/task.hpp"
#include "coro/task_container.hpp"
#include "coro/thread_pool.hpp"
#include "coro/when_all.hpp"
