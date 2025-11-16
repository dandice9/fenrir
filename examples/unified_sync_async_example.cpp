#include <fenrir/fenrir.hpp>
#include <boost/asio.hpp>
#include <iostream>

using namespace fenrir;
namespace net = boost::asio;

// Example showing unified sync and async operations on the same connection

// Sync function using the same connection
void sync_operations(database_connection& conn) {
    std::cout << "=== Synchronous Operations ===" << std::endl;
    
    // Regular synchronous query
    database_query query(conn);
    auto result = query.raw("SELECT * FROM users WHERE id = $1", 1);
    
    std::cout << "Found " << result.row_count() << " users (sync)" << std::endl;
}

// Async function using the same connection
net::awaitable<void> async_operations(database_connection& conn) {
    std::cout << "\n=== Asynchronous Operations ===" << std::endl;
    
    // Async query - non-blocking
    auto result = co_await conn.async_execute("SELECT * FROM users WHERE active = true");
    std::cout << "Found " << result.row_count() << " active users (async)" << std::endl;
    
    // Async parameterized query
    auto result2 = co_await conn.async_execute_params(
        "SELECT * FROM users WHERE email = $1", 
        "user@example.com"
    );
    std::cout << "Email lookup returned " << result2.row_count() << " rows (async)" << std::endl;
    
    // Async prepared statement
    co_await conn.async_prepare("get_user_by_id", "SELECT * FROM users WHERE id = $1");
    auto result3 = co_await conn.async_execute_prepared("get_user_by_id", 42);
    std::cout << "Prepared statement returned " << result3.row_count() << " rows (async)" << std::endl;
    
    co_return;
}

// Example 1: Using a single connection with both sync and async
net::awaitable<void> example_single_connection(net::io_context& ioc) {
    std::cout << "\n### Example 1: Single Connection - Sync + Async ###\n" << std::endl;
    
    // Create connection
    database_connection conn{
        .host = "localhost",
        .database = "mydb",
        .user = "user",
        .password = "pass"
    };
    
    // Enable async operations by setting io_context
    conn.set_io_context(ioc);
    
    // Now the same connection supports both!
    sync_operations(conn);                // Uses execute()
    co_await async_operations(conn);      // Uses async_execute()
    
    std::cout << "\n✓ Both sync and async work on the same connection!" << std::endl;
}

// Example 2: Using connection pool with async support
net::awaitable<void> example_pool_with_async(database_pool& pool) {
    std::cout << "\n### Example 2: Connection Pool with Async ###\n" << std::endl;
    
    // Acquire connection from pool (already has io_context set)
    auto conn = pool.acquire();
    
    // Use sync methods
    database_query query(*conn);
    auto result1 = query.raw("SELECT COUNT(*) FROM users");
    std::cout << "Total users: " << result1.get<int>(0, 0).value_or(0) << " (sync)" << std::endl;
    
    // Use async methods on same connection
    auto result2 = co_await conn->async_execute("SELECT COUNT(*) FROM products");
    std::cout << "Total products: " << result2.get<int>(0, 0).value_or(0) << " (async)" << std::endl;
    
    // Connection automatically returned to pool when 'conn' goes out of scope
    std::cout << "✓ Pool connection supports both sync and async!" << std::endl;
}

// Example 3: Parallel async queries (better performance)
net::awaitable<void> example_parallel_async(database_pool& pool) {
    std::cout << "\n### Example 3: Parallel Async Queries ###\n" << std::endl;
    
    // Get multiple connections
    auto conn1 = pool.acquire();
    auto conn2 = pool.acquire();
    auto conn3 = pool.acquire();
    
    // Launch queries in parallel using co_spawn
    auto executor = co_await net::this_coro::executor;
    
    auto task1 = net::co_spawn(executor, 
        conn1->async_execute("SELECT COUNT(*) FROM users"),
        net::use_awaitable
    );
    
    auto task2 = net::co_spawn(executor,
        conn2->async_execute("SELECT COUNT(*) FROM orders"),
        net::use_awaitable
    );
    
    auto task3 = net::co_spawn(executor,
        conn3->async_execute("SELECT COUNT(*) FROM products"),
        net::use_awaitable
    );
    
    // Wait for all to complete
    auto [r1, r2, r3] = co_await std::tuple{
        std::move(task1),
        std::move(task2),
        std::move(task3)
    };
    
    std::cout << "Users: " << r1.row_count() << std::endl;
    std::cout << "Orders: " << r2.row_count() << std::endl;
    std::cout << "Products: " << r3.row_count() << std::endl;
    std::cout << "✓ All queries completed in parallel!" << std::endl;
}

// Example 4: Mixed workload - some sync, some async
net::awaitable<void> example_mixed_workload(database_pool& pool) {
    std::cout << "\n### Example 4: Mixed Workload ###\n" << std::endl;
    
    auto conn = pool.acquire();
    
    // Start with sync transaction for consistency
    with_transaction(*conn, [](database_transaction& txn) {
        txn.execute("INSERT INTO logs (message) VALUES ('Starting batch')");
    });
    
    // Do heavy lifting asynchronously
    for (int i = 0; i < 3; ++i) {
        auto result = co_await conn->async_execute_params(
            "INSERT INTO batch_items (batch_id, value) VALUES ($1, $2) RETURNING id",
            1, i * 100
        );
        std::cout << "Inserted item " << i << " (async)" << std::endl;
    }
    
    // Finish with sync transaction
    with_transaction(*conn, [](database_transaction& txn) {
        txn.execute("INSERT INTO logs (message) VALUES ('Batch complete')");
    });
    
    std::cout << "✓ Mixed sync/async workload completed!" << std::endl;
}

int main() {
    try {
        net::io_context ioc;
        
        // Example 1: Single connection with sync and async
        net::co_spawn(ioc, example_single_connection(ioc), net::detached);
        ioc.run();
        ioc.restart();
        
        // Create a pool with async support
        database_pool::pool_config pool_config{
            .connection_string = "host=localhost dbname=mydb user=user password=pass",
            .min_connections = 3,
            .max_connections = 10,
            .io_context = &ioc  // Enable async for all connections in pool
        };
        database_pool pool(pool_config);
        
        std::cout << "\nPool created with async support enabled\n" << std::endl;
        
        // Example 2: Pool with async
        net::co_spawn(ioc, example_pool_with_async(pool), net::detached);
        ioc.run();
        ioc.restart();
        
        // Example 3: Parallel async
        net::co_spawn(ioc, example_parallel_async(pool), net::detached);
        ioc.run();
        ioc.restart();
        
        // Example 4: Mixed workload
        net::co_spawn(ioc, example_mixed_workload(pool), net::detached);
        ioc.run();
        
        std::cout << "\n=== All Examples Complete ===" << std::endl;
        std::cout << "\nKey Takeaways:" << std::endl;
        std::cout << "• Same connection class supports BOTH sync and async" << std::endl;
        std::cout << "• Use execute() for synchronous operations" << std::endl;
        std::cout << "• Use async_execute() for asynchronous operations" << std::endl;
        std::cout << "• Pool can be configured with io_context for automatic async support" << std::endl;
        std::cout << "• Mix and match based on your needs in the same codebase!" << std::endl;
        
    } catch (const database_error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        if (!e.sql_state.empty()) {
            std::cerr << "SQL State: " << e.sql_state << std::endl;
        }
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
