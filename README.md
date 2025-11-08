# Fenrir 🐺

A modern, header-only C++20 wrapper for PostgreSQL's libpq library, featuring type-safe operations, RAII resource management, and extensive use of C++20 features.

## Features

✨ **Modern C++20**
- Concepts for compile-time type safety
- `std::expected` for error handling without exceptions
- `std::optional` for nullable values
- `std::format` for string formatting
- Designated initializers for clean configuration

🔒 **RAII Resource Management**
- Automatic connection cleanup
- Transaction auto-rollback on exceptions
- Connection pool with automatic return
- Savepoint management

🚀 **Performance**
- Header-only library (zero compilation overhead)
- Connection pooling with thread-safety
- Efficient parameterized queries
- Move semantics throughout

🛡️ **Safety**
- SQL injection prevention via parameterized queries
- Type-safe query results with concepts
- Compile-time interface validation
- Thread-safe connection pool

## Requirements

- **C++20 compatible compiler with std::expected (C++23 feature)**
  - Clang 14.0 or later (use Homebrew LLVM on macOS, not AppleClang)
  - GCC 11 or later
  - MSVC 19.33 or later
- **libpq** (PostgreSQL client library)
  - Install via Homebrew: `brew install libpq`
  - Or install full PostgreSQL
- **CMake** 3.20 or later

### macOS Setup

```bash
# Install libpq (lightweight client library only)
brew install libpq

# Install LLVM Clang (has std::expected support)
brew install llvm

# Use Homebrew Clang for compilation
export CXX=/opt/homebrew/opt/llvm/bin/clang++
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib"
export CPPFLAGS="-I/opt/homebrew/opt/llvm/include"
```

**Note:** AppleClang (the default on macOS) doesn't support `std::expected` yet. You must use Homebrew's LLVM.

## Installation

### Using CMake

```bash
# On macOS, use Homebrew LLVM Clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++

mkdir build && cd build
cmake .. -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_CXX_STANDARD=20
cmake --build .
sudo cmake --install .
```

### Using in Your Project

#### CMake Integration

```cmake
find_package(fenrir REQUIRED)
target_link_libraries(your_target PRIVATE fenrir::fenrir)
```

#### Direct Include

Since Fenrir is header-only, you can also just include the headers:

```cpp
#include <fenrir/fenrir.hpp>
```

## Quick Start

### Basic Connection

```cpp
#include <fenrir/fenrir.hpp>

using namespace fenrir;

int main() {
    // Method 1: Connection string
    database_connection conn("host=localhost dbname=mydb user=myuser password=mypass");
    
    // Method 2: Structured parameters (C++20 designated initializers)
    database_connection::connection_params params{
        .host = "localhost",
        .port = "5432",
        .database = "mydb",
        .user = "myuser",
        .password = "mypass",
        .connect_timeout = std::chrono::seconds(30)
    };
    database_connection conn2(params);
    
    std::cout << "Connected to: " << conn.database_name() << std::endl;
}
```

### Executing Queries

```cpp
database_connection conn("host=localhost dbname=mydb user=user password=pass");
database_query query(conn);

// Create table
query.raw("CREATE TABLE users (id SERIAL PRIMARY KEY, name VARCHAR(100), age INTEGER)");

// Parameterized insert (prevents SQL injection)
query.reset();
auto result = query.raw_params(
    "INSERT INTO users (name, age) VALUES ($1, $2) RETURNING id",
    "Alice", 30
);

if (result) {
    auto id = result->get<int>(0, "id");
    std::cout << "Inserted user with ID: " << *id << std::endl;
}
```

### Query Builder Pattern

```cpp
database_query query(conn);

auto result = query.select("name, email, age")
                  .from("users")
                  .where("age >= 18")
                  .order_by("age", false)  // descending
                  .limit(10)
                  .execute();

if (result) {
    // Range-based iteration (C++20)
    for (int row : *result) {
        auto name = result->get<std::string>(row, "name");
        auto age = result->get<int>(row, "age");
        
        if (name && age) {
            std::cout << *name << " (" << *age << ")" << std::endl;
        }
    }
}
```

### Transactions

```cpp
// Method 1: Manual commit/rollback
{
    database_transaction txn(conn, isolation_level::serializable);
    
    txn.execute_params("INSERT INTO accounts (name, balance) VALUES ($1, $2)", 
                      "Alice", "1000.00");
    txn.execute_params("INSERT INTO accounts (name, balance) VALUES ($1, $2)", 
                      "Bob", "500.00");
    
    txn.commit();
}

// Method 2: RAII with automatic rollback
try {
    database_transaction txn(conn);
    
    txn.execute("UPDATE accounts SET balance = balance - 100 WHERE name = 'Alice'");
    txn.execute("UPDATE accounts SET balance = balance + 100 WHERE name = 'Bob'");
    
    // Automatic rollback if exception occurs
    throw std::runtime_error("Error!");
    
    txn.commit();  // Never reached
} catch (...) {
    std::cout << "Transaction rolled back" << std::endl;
}

// Method 3: Helper function with C++20 concepts
with_transaction(conn, [](database_transaction& txn) {
    txn.execute("INSERT INTO logs (message) VALUES ('Transaction started')");
});
```

### Savepoints

```cpp
database_transaction txn(conn);

txn.execute("INSERT INTO users (name) VALUES ('Alice')");

{
    auto sp1 = txn.create_savepoint("sp1");
    txn.execute("INSERT INTO users (name) VALUES ('Bob')");
    sp1.rollback();  // Only Bob is rolled back
}

txn.commit();  // Alice is committed
```

### Connection Pooling

```cpp
// Create connection pool
database_pool::pool_config config{
    .connection_string = "host=localhost dbname=mydb user=user password=pass",
    .min_connections = 5,
    .max_connections = 20,
    .connection_timeout = std::chrono::seconds(30),
    .validate_on_acquire = true
};

database_pool pool(config);

// Acquire connection (RAII - automatically returned to pool)
{
    auto conn = pool.acquire();
    if (conn) {
        auto result = conn->execute("SELECT * FROM users");
        // ... use connection
    }
}  // Connection automatically returned

// Thread-safe concurrent access
std::vector<std::thread> threads;
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&pool]() {
        auto conn = pool.acquire();
        if (conn) {
            conn->execute("SELECT 1");
        }
    });
}

for (auto& t : threads) t.join();

// Get pool statistics
auto stats = pool.get_stats();
std::cout << "Active: " << stats.active_connections 
          << ", Available: " << stats.available_connections << std::endl;
```

### Stored Procedures

```cpp
// Create a PostgreSQL function
conn.execute(
    "CREATE OR REPLACE FUNCTION add_numbers(a INTEGER, b INTEGER) "
    "RETURNS INTEGER AS $$ "
    "BEGIN RETURN a + b; END; "
    "$$ LANGUAGE plpgsql"
);

// Call the function
database_stored_procedure proc(conn, "add_numbers");
proc.add_param("a", 10)
    .add_param("b", 20);

auto result = proc.execute_scalar<int>();
if (result && *result) {
    std::cout << "Result: " << **result << std::endl;  // 30
}
```

## Error Handling

Fenrir uses C++20's `std::expected` for error handling without exceptions:

```cpp
auto result = conn.execute("SELECT * FROM users");

if (result) {
    // Success - use result
    PGresult* pg_result = *result;
    PQclear(pg_result);
} else {
    // Error - access error information
    const database_error& error = result.error();
    std::cerr << "Error: " << error.message << std::endl;
    std::cerr << "SQL State: " << error.sql_state << std::endl;
    std::cerr << "Location: " << error.location.file_name() << ":" 
              << error.location.line() << std::endl;
}
```

## Type Safety

Query results use `std::optional` for nullable values:

```cpp
auto result = query.raw("SELECT name, age FROM users WHERE id = 1");

if (result && result->row_count() > 0) {
    // Type-safe conversion with std::optional
    auto name = result->get<std::string>(0, "name");
    auto age = result->get<int>(0, "age");
    
    if (name && age) {
        std::cout << *name << " is " << *age << " years old" << std::endl;
    } else {
        std::cout << "NULL value encountered" << std::endl;
    }
}
```

Supported types for automatic conversion:
- `std::string`, `std::string_view`
- `int`, `long`, `long long`
- `float`, `double`
- `bool`

## C++20 Features Used

### Concepts

```cpp
template<typename T>
concept ConnectionString = std::convertible_to<T, std::string_view>;

// Ensures only valid connection string types are accepted
template<ConnectionString T>
explicit database_connection(T&& conn_str);
```

### std::format

```cpp
std::string query = std::format(
    "SELECT * FROM users WHERE age > {} AND name LIKE '%{}%'",
    18, "John"
);
```

### Designated Initializers

```cpp
database_connection::connection_params params{
    .host = "localhost",
    .database = "mydb",
    .user = "myuser",
    .password = "mypass"
};
```

### Ranges and Iterators

```cpp
auto result = query.execute();
for (int row : *result) {
    // Process each row
}
```

## Building Tests

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build .
ctest --verbose
```

### Individual Test Files

- `connection_test.cpp` - Connection management tests
- `query_test.cpp` - Query execution and result handling
- `transaction_test.cpp` - Transaction and savepoint tests
- `pool_test.cpp` - Connection pool tests

## Examples

See the `examples/` directory for complete working examples:

```bash
./build/usage_example
```

## API Reference

### database_connection

Main connection class with RAII management.

**Methods:**
- `is_connected()` - Check connection status
- `execute(query)` - Execute simple query
- `execute_params(query, args...)` - Execute parameterized query
- `database_name()`, `user_name()`, `host()`, `port()` - Connection info
- `reset()` - Reset connection
- `close()` - Close connection

### database_query

Query builder and execution class.

**Methods:**
- `select(columns)` - Start SELECT query
- `from(table)` - Specify table
- `where(condition)` - Add WHERE clause
- `order_by(column, asc)` - Add ORDER BY
- `limit(n)` - Add LIMIT
- `execute()` - Execute built query
- `raw(sql)` - Execute raw SQL
- `raw_params(sql, args...)` - Execute parameterized SQL

### query_result

RAII result set wrapper.

**Methods:**
- `row_count()` - Number of rows
- `column_count()` - Number of columns
- `get<T>(row, col)` - Get typed value
- `is_null(row, col)` - Check for NULL
- `begin()`, `end()` - Iterator support

### database_transaction

Transaction management with RAII.

**Methods:**
- `commit()` - Commit transaction
- `rollback()` - Rollback transaction
- `create_savepoint(name)` - Create savepoint
- `execute(sql)` - Execute within transaction
- `is_active()` - Check if active

### database_pool

Thread-safe connection pool.

**Methods:**
- `acquire(timeout)` - Acquire connection
- `get_stats()` - Get pool statistics
- `shutdown()` - Close all connections

### database_stored_procedure

Stored procedure wrapper.

**Methods:**
- `add_param(name, value)` - Add input parameter
- `add_out_param(name)` - Add output parameter
- `execute()` - Execute procedure
- `execute_scalar<T>()` - Execute and get single value

## Performance Tips

1. **Use Connection Pooling** - Reuse connections instead of creating new ones
2. **Parameterized Queries** - More efficient than string concatenation
3. **Transactions** - Batch operations for better performance
4. **Prepared Statements** - Use for repeated queries (via parameterized queries)
5. **Connection Validation** - Enable `validate_on_acquire` for long-lived pools

## Thread Safety

- ✅ `database_pool` - Fully thread-safe
- ❌ `database_connection` - Not thread-safe (use pool for concurrent access)
- ❌ `database_transaction` - Not thread-safe (one transaction per connection)
- ❌ `database_query` - Not thread-safe (create per thread or use pool)

## License

MIT License - See LICENSE file for details

## Contributing

Contributions are welcome! Please ensure:
- Code compiles with C++20
- Tests pass
- Follow existing code style
- Add tests for new features

## Acknowledgments

Built on top of PostgreSQL's excellent libpq library. Designed to leverage modern C++20 features for type safety and ergonomic APIs.

## Support

For issues, questions, or contributions, please visit the GitHub repository.

---

Made with ❤️ using C++20 and PostgreSQL
