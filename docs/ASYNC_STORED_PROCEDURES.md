# Async Stored Procedure Support

## Overview

Fenrir's `database_stored_procedure` now supports **asynchronous execution** using C++20 coroutines, enabling non-blocking database operations perfect for high-performance web servers.

## Key Features

✅ **Non-Blocking**: Stored procedures execute without blocking threads  
✅ **C++20 Coroutines**: Clean async code with `co_await`  
✅ **Same API**: Familiar interface for both sync and async  
✅ **Type-Safe**: Template-based parameter handling  
✅ **Error Handling**: Exception-based error reporting with SQL state codes

## Quick Comparison

### Synchronous (Blocking)

```cpp
database_stored_procedure proc(conn, "get_user");
proc.add_param("user_id", 123);

auto result = proc.execute();  // ❌ Blocks thread until complete
auto user = result.get<std::string>(0, "username");
```

**Problem**: While waiting for database, thread is blocked and can't handle other requests.

### Asynchronous (Non-Blocking)

```cpp
database_stored_procedure proc(conn, "get_user");
proc.add_param("user_id", 123);

auto result = co_await proc.async_execute();  // ✅ Non-blocking!
auto user = result.get<std::string>(0, "username");
```

**Benefit**: Thread can handle other requests while waiting for database response.

## API Reference

### Async Methods

```cpp
// Execute stored procedure asynchronously
[[nodiscard]] net::awaitable<query_result> async_execute();

// Execute scalar function asynchronously (returns single value)
template<typename T>
[[nodiscard]] net::awaitable<std::optional<T>> async_execute_scalar();
```

### Synchronous Methods (Still Available)

```cpp
// Execute stored procedure synchronously
[[nodiscard]] query_result execute();

// Execute scalar function synchronously
template<typename T>
[[nodiscard]] std::optional<T> execute_scalar();
```

## Usage Examples

### 1. Basic Async Execution

```cpp
#include "fenrir.hpp"
#include <boost/asio.hpp>

namespace net = boost::asio;

net::awaitable<void> create_user(database_connection& conn) {
    database_stored_procedure proc(conn, "create_user");
    
    proc.add_param("username", "alice")
        .add_param("email", "alice@example.com")
        .add_param("age", 28);
    
    // Execute asynchronously
    auto result = co_await proc.async_execute();
    
    std::cout << "User created! ID: " 
              << result.get<int>(0, "id") << "\n";
}

int main() {
    net::io_context ioc;
    database_connection conn("host=localhost dbname=mydb");
    
    net::co_spawn(ioc, create_user(conn), net::detached);
    ioc.run();
}
```

### 2. Async Scalar Functions

```cpp
net::awaitable<void> get_statistics(database_connection& conn) {
    // Get user count
    database_stored_procedure count_proc(conn, "get_total_users");
    auto user_count = co_await count_proc.async_execute_scalar<int>();
    
    if (user_count) {
        std::cout << "Total users: " << *user_count << "\n";
    }
    
    // Get average age
    database_stored_procedure avg_proc(conn, "get_average_user_age");
    auto avg_age = co_await avg_proc.async_execute_scalar<double>();
    
    if (avg_age) {
        std::cout << "Average age: " << *avg_age << "\n";
    }
}
```

### 3. Sequential Async Operations

```cpp
net::awaitable<void> update_user_workflow(
    database_connection& conn, 
    int user_id) {
    
    // Step 1: Get user data
    database_stored_procedure get_proc(conn, "get_user");
    get_proc.add_param("user_id", user_id);
    auto user = co_await get_proc.async_execute();
    
    // Step 2: Update user status
    database_stored_procedure update_proc(conn, "update_user_status");
    update_proc.add_param("user_id", user_id)
              .add_param("status", "active");
    co_await update_proc.async_execute();
    
    // Step 3: Log the change
    database_stored_procedure log_proc(conn, "log_user_change");
    log_proc.add_param("user_id", user_id)
            .add_param("action", "status_updated");
    co_await log_proc.async_execute();
    
    std::cout << "User workflow completed!\n";
}
```

### 4. Error Handling

```cpp
net::awaitable<void> safe_procedure_call(database_connection& conn) {
    try {
        database_stored_procedure proc(conn, "risky_operation");
        proc.add_param("amount", 1000);
        
        auto result = co_await proc.async_execute();
        std::cout << "Operation succeeded!\n";
        
    } catch (const database_error& e) {
        std::cerr << "Database error: " << e.what() << "\n";
        std::cerr << "SQL State: " << e.sql_state << "\n";
        
        // Handle specific errors
        if (e.sql_state == "23505") {
            std::cerr << "Duplicate key violation\n";
        } else if (e.sql_state == "23503") {
            std::cerr << "Foreign key violation\n";
        }
    }
}
```

## Integration with Wolf Web Server

Perfect combination: Async stored procedures + Async web handlers!

```cpp
#include "fenrir.hpp"
#include "wolf.hpp"

int main() {
    // Setup database pool
    fenrir::database_pool pool(
        "host=localhost dbname=myapp",
        5  // connection pool size
    );
    
    // Create web server
    wolf::web_server server(8080);
    
    // ✅ Async handler with async stored procedure
    server->get("/api/users/:id", 
        [&pool](const wolf::http_request& req) 
        -> net::awaitable<wolf::http_response> {
        
        auto id = std::stoi(req.params().at("id"));
        
        try {
            // Get connection from pool (async)
            auto conn = co_await pool.async_acquire();
            
            // Call stored procedure (async)
            fenrir::database_stored_procedure proc(*conn, "get_user_details");
            proc.add_param("user_id", id);
            
            auto result = co_await proc.async_execute();
            
            if (result.row_count() == 0) {
                co_return wolf::http_response(404).json({
                    {"error", "User not found"}
                });
            }
            
            boost::json::object user = {
                {"id", result.get<int>(0, "id")},
                {"username", result.get<std::string>(0, "username")},
                {"email", result.get<std::string>(0, "email")}
            };
            
            co_return wolf::http_response(200).json(user);
            
        } catch (const fenrir::database_error& e) {
            co_return wolf::http_response(500).json({
                {"error", "Database error"},
                {"message", e.what()}
            });
        }
    });
    
    // Create user endpoint
    server->post("/api/users",
        [&pool](const wolf::http_request& req)
        -> net::awaitable<wolf::http_response> {
        
        try {
            auto body = req.get_json_body().as_object();
            
            auto conn = co_await pool.async_acquire();
            
            fenrir::database_stored_procedure proc(*conn, "create_user");
            proc.add_param("username", body["username"].as_string())
                .add_param("email", body["email"].as_string())
                .add_param("age", body["age"].as_int64());
            
            auto result = co_await proc.async_execute();
            auto new_id = result.get<int>(0, "id");
            
            co_return wolf::http_response(201).json({
                {"success", true},
                {"id", new_id}
            });
            
        } catch (const fenrir::database_error& e) {
            co_return wolf::http_response(500).json({
                {"error", e.what()}
            });
        }
    });
    
    std::cout << "Server running on http://localhost:8080\n";
    server.start();
}
```

## Performance Benefits

### Synchronous Approach

```
Request 1: [Wait DB] -------- [Process] -> Response
Request 2:           [Wait DB] -------- [Process] -> Response
Request 3:                     [Wait DB] -------- [Process] -> Response

Thread blocked during [Wait DB]
```

### Asynchronous Approach

```
Request 1: [Wait DB] -------- [Process] -> Response
Request 2: [Wait DB] -------- [Process] -> Response
Request 3: [Wait DB] -------- [Process] -> Response

Thread handles other work during [Wait DB]
Multiple requests can wait concurrently
```

**Benefits:**
- 🚀 **Higher Throughput**: Handle more requests with same threads
- ⚡ **Lower Latency**: No thread blocking means faster response times
- 💰 **Resource Efficient**: Fewer threads needed for same workload
- 📈 **Better Scaling**: Graceful degradation under load

## Best Practices

### ✅ DO

```cpp
// Use async in web handlers
server->get("/users", [&pool](auto req) -> net::awaitable<wolf::http_response> {
    auto conn = co_await pool.async_acquire();
    
    database_stored_procedure proc(*conn, "list_users");
    auto result = co_await proc.async_execute();
    
    co_return wolf::http_response(200).json(build_json(result));
});

// Use async for I/O-bound operations
net::awaitable<void> process_batch() {
    for (int i = 0; i < 100; i++) {
        auto result = co_await proc.async_execute();
        // Process result...
    }
}
```

### ❌ DON'T

```cpp
// Don't use sync in async handlers (blocks thread!)
server->get("/bad", [&pool](auto req) -> net::awaitable<wolf::http_response> {
    auto conn = co_await pool.async_acquire();
    
    database_stored_procedure proc(*conn, "get_data");
    auto result = proc.execute();  // ❌ Blocks! Use async_execute()
    
    co_return wolf::http_response(200).json(data);
});

// Don't mix sync I/O in async context
net::awaitable<void> bad_example() {
    auto result1 = co_await proc.async_execute();  // ✅ Good
    std::this_thread::sleep_for(1s);  // ❌ Bad! Use async timer
    auto result2 = co_await proc.async_execute();  // ✅ Good
}
```

## When to Use Sync vs Async

### Use Synchronous (`execute()`)

- ✅ Simple scripts or CLI tools
- ✅ Single-threaded applications
- ✅ Batch processing where blocking is acceptable
- ✅ Startup/initialization code

### Use Asynchronous (`async_execute()`)

- ✅ Web servers handling concurrent requests
- ✅ Real-time applications
- ✅ High-throughput systems
- ✅ Any I/O-bound workload

## Requirements

- **C++20** compiler with coroutine support
- **Boost.Asio** 1.81+ with coroutines
- **libpq** (PostgreSQL client library)

## Example SQL Setup

```sql
-- Simple function
CREATE OR REPLACE FUNCTION get_total_users()
RETURNS INTEGER AS $$
BEGIN
    RETURN (SELECT COUNT(*) FROM users);
END;
$$ LANGUAGE plpgsql;

-- Function with parameters
CREATE OR REPLACE FUNCTION create_user(
    p_username TEXT,
    p_email TEXT,
    p_age INTEGER
)
RETURNS TABLE(id INTEGER, username TEXT, created_at TIMESTAMP) AS $$
BEGIN
    RETURN QUERY
    INSERT INTO users (username, email, age)
    VALUES (p_username, p_email, p_age)
    RETURNING users.id, users.username, users.created_at;
END;
$$ LANGUAGE plpgsql;

-- Function returning result set
CREATE OR REPLACE FUNCTION get_user_details(p_user_id INTEGER)
RETURNS TABLE(
    id INTEGER,
    username TEXT,
    email TEXT,
    age INTEGER,
    created_at TIMESTAMP
) AS $$
BEGIN
    RETURN QUERY
    SELECT u.id, u.username, u.email, u.age, u.created_at
    FROM users u
    WHERE u.id = p_user_id;
END;
$$ LANGUAGE plpgsql;
```

## Summary

The async stored procedure support enables:

✅ **Non-blocking database operations**  
✅ **Clean coroutine-based code**  
✅ **Perfect for async web servers**  
✅ **Higher throughput and better resource usage**  
✅ **Same familiar API as synchronous version**

Use `async_execute()` and `async_execute_scalar()` for I/O-bound operations, especially in web servers and high-concurrency applications!
