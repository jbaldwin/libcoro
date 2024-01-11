#pragma once

#include "coro/concepts/awaitable.hpp"
#include "coro/concepts/buffer.hpp"
#include "coro/concepts/executor.hpp"
#include "coro/concepts/promise.hpp"
#include "coro/concepts/range_of.hpp"

#include "coro/expected.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/io_scheduler.hpp"
    #include "coro/net/dns/resolver.hpp"
    #include "coro/net/tcp/client.hpp"
    #include "coro/net/tcp/server.hpp"
    #include "coro/poll.hpp"
    #ifdef LIBCORO_FEATURE_TLS
        #include "coro/net/tls/client.hpp"
        #include "coro/net/tls/connection_status.hpp"
        #include "coro/net/tls/context.hpp"
        #include "coro/net/tls/server.hpp"
    #endif
    #include "coro/net/connect.hpp"
    #include "coro/net/hostname.hpp"
    #include "coro/net/ip_address.hpp"
    #include "coro/net/recv_status.hpp"
    #include "coro/net/send_status.hpp"
    #include "coro/net/socket.hpp"
    #include "coro/net/udp/peer.hpp"
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
#include "coro/time.hpp"
#include "coro/when_all.hpp"
