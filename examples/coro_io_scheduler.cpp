#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{
        // The scheduler will spawn a dedicated event processing thread.  This is the default, but
        // it is possible to use 'manual' and call 'process_events()' to drive the scheduler yourself.
        .thread_strategy = coro::io_scheduler::thread_strategy_t::spawn,
        // If the scheduler is in spawn mode this functor is called upon starting the dedicated
        // event processor thread.
        .on_io_thread_start_functor = [] { std::cout << "io_scheduler::process event thread start\n"; },
        // If the scheduler is in spawn mode this functor is called upon stopping the dedicated
        // event process thread.
        .on_io_thread_stop_functor = [] { std::cout << "io_scheduler::process event thread stop\n"; },
        // The io scheduler can use a coro::thread_pool to process the events or tasks it is given.
        // You can use an execution strategy of `process_tasks_inline` to have the event loop thread
        // directly process the tasks, this might be desirable for small tasks vs a thread pool for large tasks.
        .pool =
            coro::thread_pool::options{
                .thread_count            = 2,
                .on_thread_start_functor = [](size_t i)
                { std::cout << "io_scheduler::thread_pool worker " << i << " starting\n"; },
                .on_thread_stop_functor = [](size_t i)
                { std::cout << "io_scheduler::thread_pool worker " << i << " stopping\n"; },
            },
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});

    auto make_server_task = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<void>
    {
        // Start by creating a tcp server, we'll do this before putting it into the scheduler so
        // it is immediately available for the client to connect since this will create a socket,
        // bind the socket and start listening on that socket.  See tcp::server for more details on
        // how to specify the local address and port to bind to as well as enabling SSL/TLS.
        coro::net::tcp::server server{scheduler};

        // Now scheduler this task onto the scheduler.
        co_await scheduler->schedule();

        // Wait for an incoming connection and accept it.
        auto poll_status = co_await server.poll();
        if (poll_status != coro::poll_status::event)
        {
            co_return; // Handle error, see poll_status for detailed error states.
        }

        // Accept the incoming client connection.
        auto client = server.accept();

        // Verify the incoming connection was accepted correctly.
        if (!client.socket().is_valid())
        {
            co_return; // Handle error.
        }

        // Now wait for the client message, this message is small enough it should always arrive
        // with a single recv() call.
        poll_status = co_await client.poll(coro::poll_op::read);
        if (poll_status != coro::poll_status::event)
        {
            co_return; // Handle error.
        }

        // Prepare a buffer and recv() the client's message.  This function returns the recv() status
        // as well as a span<char> that overlaps the given buffer for the bytes that were read.  This
        // can be used to resize the buffer or work with the bytes without modifying the buffer at all.
        std::string request(256, '\0');
        auto [recv_status, recv_bytes] = client.recv(request);
        if (recv_status != coro::net::recv_status::ok)
        {
            co_return; // Handle error, see net::recv_status for detailed error states.
        }

        request.resize(recv_bytes.size());
        std::cout << "server: " << request << "\n";

        // Make sure the client socket can be written to.
        poll_status = co_await client.poll(coro::poll_op::write);
        if (poll_status != coro::poll_status::event)
        {
            co_return; // Handle error.
        }

        // Send the server response to the client.
        // This message is small enough that it will be sent in a single send() call, but to demonstrate
        // how to use the 'remaining' portion of the send() result this is wrapped in a loop until
        // all the bytes are sent.
        std::string           response  = "Hello from server.";
        std::span<const char> remaining = response;
        do
        {
            // Optimistically send() prior to polling.
            auto [send_status, r] = client.send(remaining);
            if (send_status != coro::net::send_status::ok)
            {
                co_return; // Handle error, see net::send_status for detailed error states.
            }

            if (r.empty())
            {
                break; // The entire message has been sent.
            }

            // Re-assign remaining bytes for the next loop iteration and poll for the socket to be
            // able to be written to again.
            remaining    = r;
            auto pstatus = co_await client.poll(coro::poll_op::write);
            if (pstatus != coro::poll_status::event)
            {
                co_return; // Handle error.
            }
        } while (true);

        co_return;
    };

    auto make_client_task = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        // Create the tcp::client with the default settings, see tcp::client for how to set the
        // ip address, port, and optionally enabling SSL/TLS.
        coro::net::tcp::client client{scheduler};

        // Ommitting error checking code for the client, each step should check the status and
        // verify the number of bytes sent or received.

        // Connect to the server.
        co_await client.connect();

        // Make sure the client socket can be written to.
        co_await client.poll(coro::poll_op::write);

        // Send the request data.
        client.send(std::string_view{"Hello from client."});

        // Wait for the response and receive it.
        co_await client.poll(coro::poll_op::read);
        std::string response(256, '\0');
        auto [recv_status, recv_bytes] = client.recv(response);
        response.resize(recv_bytes.size());

        std::cout << "client: " << response << "\n";
        co_return;
    };

    // Create and wait for the server and client tasks to complete.
    coro::sync_wait(coro::when_all(make_server_task(scheduler), make_client_task(scheduler)));
}
