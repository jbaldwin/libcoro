#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = coro::scheduler::make_unique(
        coro::scheduler::options{
            // The scheduler will spawn a dedicated event processing thread.  This is the default, but
            // it is possible to use 'manual' and call 'process_events()' to drive the scheduler yourself.
            .thread_strategy = coro::scheduler::thread_strategy_t::spawn,
            // If the scheduler is in spawn mode this functor is called upon starting the dedicated
            // event processor thread.
            .on_io_thread_start_functor = [] { std::cout << "scheduler::process event thread start\n"; },
            // If the scheduler is in spawn mode this functor is called upon stopping the dedicated
            // event process thread.
            .on_io_thread_stop_functor = [] { std::cout << "scheduler::process event thread stop\n"; },
            // The io scheduler can use a coro::thread_pool to process the events or tasks it is given.
            // You can use an execution strategy of `process_tasks_inline` to have the event loop thread
            // directly process the tasks, this might be desirable for small tasks vs a thread pool for large tasks.
            .pool =
                coro::thread_pool::options{
                    .thread_count            = 2,
                    .on_thread_start_functor = [](size_t i)
                    { std::cout << "scheduler::thread_pool worker " << i << " starting\n"; },
                    .on_thread_stop_functor = [](size_t i)
                    { std::cout << "scheduler::thread_pool worker " << i << " stopping\n"; },
                },
            .execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_on_thread_pool});

    auto make_server_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        // Start by creating a tcp server, we'll do this before putting it into the scheduler so
        // it is immediately available for the client to connect since this will create a socket,
        // bind the socket and start listening on that socket.
        coro::net::tcp::server server{scheduler, {"127.0.0.1", 8080}};

        // Now schedule this task onto the scheduler.
        co_await scheduler->schedule();

        // Wait for an incoming connection and accept it.
        auto client = co_await server.accept();

        // Verify the incoming connection was accepted correctly.
        if (!client)
        {
            std::cout << "server error: " << client.error().message() << "\n";
            co_return; // Handle error.
        }

        // Prepare a buffer and read_some() the client's message.  This function returns the read_some() status
        // as well as a span<std::byte> that overlaps the given buffer for the bytes that were read. This
        // can be used to resize the buffer or work with the bytes without modifying the buffer at all.
        std::string request(256, '\0');
        auto [read_status, read_bytes] = co_await client->read_some(request);
        if (!read_status.is_ok())
        {
            std::cout << "server error: " << read_status.message() << "\n";
            co_return; // Handle error, see net::io_status for detailed error stats.
        }

        request.resize(read_bytes.size());
        std::cout << "server: " << request << "\n";

        // Send the server response to the client.
        std::string response        = "Hello from server.";
        auto [write_status, unsent] = co_await client->write_all(response);
        if (!write_status.is_ok())
        {
            std::cout << "server error: " << write_status.message() << "\n";
            co_return; // Handle error, see net::io_status for detailed error stats.
        }

        co_return;
    };

    auto make_client_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        // Create the tcp::client
        coro::net::tcp::client client{scheduler, {"127.0.0.1", 8080}};

        // Ommitting error checking code for the client, each step should check the status and
        // verify the number of bytes sent or received.

        // Connect to the server.
        co_await client.connect();

        // Send the request data.
        co_await client.write_all(std::string_view{"Hello from client."});

        // Wait for the response and receive it.
        std::string response(256, '\0');
        auto [read_status, read_bytes] = co_await client.read_some(response);
        response.resize(read_bytes.size());

        std::cout << "client: " << response << "\n";
        co_return;
    };

    // Create and wait for the server and client tasks to complete.
    coro::sync_wait(coro::when_all(make_server_task(scheduler), make_client_task(scheduler)));
}
