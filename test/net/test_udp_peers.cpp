#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

TEST_CASE("udp one way")
{
    const std::string msg{"aaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbcccccccccccccccccc"};

    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_send_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::udp_peer       peer{scheduler};
        coro::net::udp_peer::info peer_info{};

        auto [sstatus, remaining] = peer.sendto(peer_info, msg);
        REQUIRE(sstatus == coro::net::send_status::ok);
        REQUIRE(remaining.empty());

        co_return;
    };

    auto make_recv_task = [&]() -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::udp_peer::info self_info{.address = coro::net::ip_address::from_string("0.0.0.0")};

        coro::net::udp_peer self{scheduler, self_info};

        auto pstatus = co_await self.poll(coro::poll_op::read);
        REQUIRE(pstatus == coro::poll_status::event);

        std::string buffer(64, '\0');
        auto [rstatus, peer_info, rspan] = self.recvfrom(buffer);
        REQUIRE(rstatus == coro::net::recv_status::ok);
        REQUIRE(peer_info.address == coro::net::ip_address::from_string("127.0.0.1"));
        // The peer's port will be randomly picked by the kernel since it wasn't bound.
        REQUIRE(rspan.size() == msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == msg);

        co_return;
    };

    coro::sync_wait(coro::when_all(make_recv_task(), make_send_task()));
}

TEST_CASE("udp echo peers")
{
    const std::string peer1_msg{"Hello from peer1!"};
    const std::string peer2_msg{"Hello from peer2!!"};

    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_peer_task = [&scheduler](
                              uint16_t          my_port,
                              uint16_t          peer_port,
                              bool              send_first,
                              const std::string my_msg,
                              const std::string peer_msg) -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::udp_peer::info my_info{.address = coro::net::ip_address::from_string("0.0.0.0"), .port = my_port};
        coro::net::udp_peer::info peer_info{
            .address = coro::net::ip_address::from_string("127.0.0.1"), .port = peer_port};

        coro::net::udp_peer me{scheduler, my_info};

        if (send_first)
        {
            // Send my message to my peer first.
            auto [sstatus, remaining] = me.sendto(peer_info, my_msg);
            REQUIRE(sstatus == coro::net::send_status::ok);
            REQUIRE(remaining.empty());
        }
        else
        {
            // Poll for my peers message first.
            auto pstatus = co_await me.poll(coro::poll_op::read);
            REQUIRE(pstatus == coro::poll_status::event);

            std::string buffer(64, '\0');
            auto [rstatus, recv_peer_info, rspan] = me.recvfrom(buffer);
            REQUIRE(rstatus == coro::net::recv_status::ok);
            REQUIRE(recv_peer_info == peer_info);
            REQUIRE(rspan.size() == peer_msg.size());
            buffer.resize(rspan.size());
            REQUIRE(buffer == peer_msg);
        }

        if (send_first)
        {
            // I sent first so now I need to await my peer's message.
            auto pstatus = co_await me.poll(coro::poll_op::read);
            REQUIRE(pstatus == coro::poll_status::event);

            std::string buffer(64, '\0');
            auto [rstatus, recv_peer_info, rspan] = me.recvfrom(buffer);
            REQUIRE(rstatus == coro::net::recv_status::ok);
            REQUIRE(recv_peer_info == peer_info);
            REQUIRE(rspan.size() == peer_msg.size());
            buffer.resize(rspan.size());
            REQUIRE(buffer == peer_msg);
        }
        else
        {
            auto [sstatus, remaining] = me.sendto(peer_info, my_msg);
            REQUIRE(sstatus == coro::net::send_status::ok);
            REQUIRE(remaining.empty());
        }

        co_return;
    };

    coro::sync_wait(coro::when_all(
        make_peer_task(8081, 8080, false, peer2_msg, peer1_msg),
        make_peer_task(8080, 8081, true, peer1_msg, peer2_msg)));
}
