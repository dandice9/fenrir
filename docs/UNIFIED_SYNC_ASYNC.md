# Unified Sync/Async Database Operations

Fenrir now supports **both synchronous and asynchronous** operations on the same `database_connection` class. You can mix and match based on your needs!

## Key Features

✅ **Single Connection Class** - No separate `async_database_connection`  
✅ **Choose Per-Operation** - Use `execute()` or `async_execute()` as needed  
✅ **Pool Support** - Connection pool can provide async-ready connections  
✅ **Zero Overhead** - Async features only activate when you set an `io_context`  
✅ **Backward Compatible** - Existing sync code works unchanged  

## Quick Start

### 1. Synchronous Operations (Traditional)

```cpp
#include <fenrir/fenrir.hpp>

database_connection conn("host=localhost dbname=mydb user=user password=pass");

// Synchronous query - blocks until complete
database_query query(conn);
auto result = query.raw("SELECT * FROM users");
```

### 2. Asynchronous Operations (New!)

```cpp
#include <fenrir/fenrir.hpp>
#include <boost/asio.hpp>

boost::asio::io_context ioc;
database_connection conn("host=localhost dbname=mydb user=user password=pass");

// Enable async by setting io_context
conn.set_io_context(ioc);

// Now use async methods
auto async_task = [&]() -> boost::asio::awaitable<void> {
    // Async query - non-blocking
    auto result = co_await conn.async_execute("SELECT * FROM users");
    std::cout << "Found " << result.row_count() << " users" << std::endl;
};

boost::asio::co_spawn(ioc, async_task(), boost::asio::detached);
ioc.run();
```

### 3. Mix Both in Same Connection!

```cpp
database_connection conn("...");
conn.set_io_context(ioc);

// Use sync when you want simplicity
database_query query(conn);
auto sync_result = query.raw("SELECT * FROM settings");

// Use async when you want performance
auto async_func = [&]() -> boost::asio::awaitable<void> {
    auto async_result = co_await conn.async_execute("SELECT * FROM large_table");
    // Process while other operations continue...
    co_return;
};
```

## Connection Pool with Async

Enable async for all connections in a pool:

```cpp
boost::asio::io_context ioc;

database_pool::pool_config config{
    .connection_string = "host=localhost dbname=mydb user=user password=pass",
    .min_connections = 5,
    .max_connections = 20,
    .io_context = &ioc  // 👈 Enable async for entire pool
};

database_pool pool(config);

// All acquired connections support both sync and async!
auto conn = pool.acquire();

// Sync
database_query(*conn).raw("SELECT 1");

// Async
co_await conn->async_execute("SELECT 2");
```

## Available Async Methods

| Sync Method | Async Method | Description |
|-------------|--------------|-------------|
| `query.execute()` | `conn.async_execute(sql)` | Execute raw SQL |
| `query.execute(sql, args...)` | `conn.async_execute_params(sql, args...)` | Parameterized query |
| `prepare()` + `execute_prepared()` | `async_prepare()` + `async_execute_prepared()` | Prepared statements |

## When to Use Sync vs Async

### Use **Synchronous** When:
- Simple CRUD operations
- Sequential workflow (one query depends on previous)
- Rapid development / prototyping
- Low concurrency requirements

### Use **Asynchronous** When:
- High concurrency (many simultaneous queries)
- Long-running queries
- I/O-bound operations
- Building web servers with async request handling
- Need to maximize throughput

### Mix Both When:
- Some operations are quick (sync), others are slow (async)
- Gradual migration from sync to async
- Different parts of app have different requirements

## Performance Comparison

```cpp
// Sequential sync queries - SLOW (waits for each)
auto r1 = query.raw("SELECT * FROM users");      // Wait
auto r2 = query.raw("SELECT * FROM orders");     // Wait
auto r3 = query.raw("SELECT * FROM products");   // Wait
// Total time: T1 + T2 + T3

// Parallel async queries - FAST (all at once)
auto task1 = conn1.async_execute("SELECT * FROM users");
auto task2 = conn2.async_execute("SELECT * FROM orders");  
auto task3 = conn3.async_execute("SELECT * FROM products");
auto [r1, r2, r3] = co_await std::tuple{task1, task2, task3};
// Total time: max(T1, T2, T3) ⚡
```

## Complete Example

See `examples/unified_sync_async_example.cpp` for comprehensive examples showing:
1. Single connection with both sync and async
2. Connection pool with async support
3. Parallel async queries
4. Mixed sync/async workload

## Migration Guide

### From Old Sync Code
```cpp
// Old code (still works!)
database_connection conn("...");
database_query query(conn);
auto result = query.raw("SELECT ...");
```

### Add Async Gradually
```cpp
// Same connection, now with async
database_connection conn("...");
conn.set_io_context(ioc);  // 👈 Add this line

database_query query(conn);
auto result = query.raw("SELECT ...");  // Still works

// New async operations
co_await conn.async_execute("SELECT ...");  // Also works!
```

## Implementation Details

- Async methods use PostgreSQL's `PQsendQuery()` family of functions
- Polling with `PQconsumeInput()` and `PQisBusy()` 
- Boost.Asio coroutines with `steady_timer` for non-blocking waits
- 1ms poll interval (configurable in implementation)
- Same connection is **NOT** thread-safe for simultaneous use
- Use connection pool for multi-threaded scenarios

## Best Practices

1. **Set io_context once** after creating connection
2. **Use pool** for managing multiple async connections
3. **Don't share connections** between threads without synchronization
4. **Prefer async for I/O bound** operations
5. **Prefer sync for CPU bound** operations or simple queries
6. **Mix freely** - no performance penalty for switching between them

## See Also

- `database_connection.hpp` - Core connection class
- `database_connection_async.hpp` - Async method implementations
- `database_pool.hpp` - Thread-safe connection pooling
- `examples/unified_sync_async_example.cpp` - Complete working examples
