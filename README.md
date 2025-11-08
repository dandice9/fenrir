# Fenrir 🐺

A modern, header-only C++20 wrapper for PostgreSQL's libpq library, featuring exception-based error handling, RAII resource management, and extensive use of C++20 features.

## Features

✨ **Modern C++20**
- Concepts for compile-time type safety
- Exception-based error handling with custom `database_error`
- `std::optional` for nullable values
- `std::format` for string formatting
- Designated initializers for clean configuration

🔒 **RAII Resource Management**
- Automatic connection cleanup
- Transaction auto-rollback on exceptions
- Connection pool with automatic return
- Query result memory management

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

- **C++20 compatible compiler**
  - AppleClang 15.0+ (default on macOS)
  - Clang 14.0 or later
  - GCC 11 or later
  - MSVC 19.33 or later
- **libpq** (PostgreSQL client library)
  - Install via Homebrew: `brew install libpq`
  - Or install full PostgreSQL: `brew install postgresql`
- **CMake** 3.20 or later (for building tests/examples)
- **Catch2** v3 (for tests, automatically fetched by CMake)

### macOS Setup

```bash
# Install PostgreSQL (includes libpq)
brew install postgresql@14

# Or just install libpq (lightweight client library)
brew install libpq

# Start PostgreSQL service (if full install)
brew services start postgresql@14
```

## Database Setup

Before running tests or examples, set up your PostgreSQL test database.

### Quick Setup (Recommended)

Run the provided setup script:

```bash
./setup_testdb.sh
```

This script will:
- Create the `testdb` database
- Create the `testuser` user with password `testpass`
- Grant all necessary privileges

### Manual Setup

If you prefer to set up manually:

```bash
# Connect to PostgreSQL
psql postgres

# In psql, run:
CREATE DATABASE testdb;
CREATE USER testuser WITH PASSWORD 'testpass';
GRANT ALL PRIVILEGES ON DATABASE testdb TO testuser;
\c testdb
GRANT ALL ON SCHEMA public TO testuser;
\q
```

### Connection Details

The tests and examples use these credentials:
- **Database:** `testdb`
- **User:** `testuser`
- **Password:** `testpass`
- **Host:** `localhost`
- **Port:** `5432` (default)

**Connection String:**
```
host=localhost port=5432 dbname=testdb user=testuser password=testpass
```

## Installation

### Building the Project

```bash
# Clone the repository
git clone <repository-url>
cd fenrir

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build all targets (tests and examples)
make -j$(nproc)

# Or use CMake's build command
cmake --build . -j$(nproc)
```

This will create executables in the `build/` directory:
- `connection_test` - Connection tests
- `query_test` - Query tests  
- `transaction_test` - Transaction tests
- `pool_test` - Connection pool tests
- `usage_example` - Usage demonstration

### Running Tests

```bash
# Run all tests
ctest --verbose

# Or run individual test executables
./connection_test
./query_test
./transaction_test
./pool_test
```

### Running the Example

```bash
./usage_example
```

### Using in Your Project

#### CMake Integration

```cmake
# Add fenrir as a subdirectory
add_subdirectory(path/to/fenrir)

# Link against fenrir
target_link_libraries(your_target PRIVATE fenrir::fenrir)
```

#### Direct Include

Since Fenrir is header-only, you can also directly include the headers:

```cpp
#include "path/to/fenrir/src/fenrir.hpp"

// Then link against libpq
// g++ your_app.cpp -lpq -std=c++20
```

## Quick Start

### Basic Connection

```cpp
#include "fenrir.hpp"

using namespace fenrir;

int main() {
    try {
        // Method 1: Connection string
        database_connection conn("host=localhost dbname=testdb user=testuser password=testpass");
        
        // Method 2: Structured parameters (C++20 designated initializers)
        database_connection::connection_params params{
            .host = "localhost",
            .port = "5432",
            .database = "testdb",
            .user = "testuser",
            .password = "testpass",
            .connect_timeout = std::chrono::seconds(30)
        };
        database_connection conn2(params);
        
        std::cout << "Connected to: " << conn.database_name() << std::endl;
        std::cout << "PostgreSQL version: " << conn.server_version() << std::endl;
        
        return 0;
    } catch (const database_error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    }
}
```

### Executing Queries

```cpp
database_connection conn("host=localhost dbname=testdb user=testuser password=testpass");

// Simple query execution
auto result = conn.execute("CREATE TABLE users (id SERIAL PRIMARY KEY, name VARCHAR(100), age INTEGER)");
PQclear(result);  // Clean up result

// Parameterized insert (prevents SQL injection)
auto insert_result = conn.execute_params(
    "INSERT INTO users (name, age) VALUES ($1, $2) RETURNING id",
    "Alice", "30"
);

query_result qr(insert_result);  // RAII wrapper for automatic cleanup
auto id = qr.get<int>(0, "id");
if (id) {
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

// result is query_result with automatic memory management
// Range-based iteration (C++20)
for (int row : result) {
    auto name = result.get<std::string>(row, "name");
    auto age = result.get<int>(row, "age");
    
    if (name && age) {
        std::cout << *name << " (" << *age << ")" << std::endl;
    }
}
```

### Transactions

```cpp
// Method 1: Manual commit/rollback
{
    database_transaction txn(conn, isolation_level::serializable);
    
    // txn.execute() returns query_result (auto-managed)
    (void)txn.execute("INSERT INTO accounts (name, balance) VALUES ('Alice', 1000.00)");
    (void)txn.execute("INSERT INTO accounts (name, balance) VALUES ('Bob', 500.00)");
    
    txn.commit();
}

// Method 2: RAII with automatic rollback
try {
    database_transaction txn(conn);
    
    (void)txn.execute("UPDATE accounts SET balance = balance - 100 WHERE name = 'Alice'");
    (void)txn.execute("UPDATE accounts SET balance = balance + 100 WHERE name = 'Bob'");
    
    // Automatic rollback if exception occurs
    throw std::runtime_error("Simulated error!");
    
    txn.commit();  // Never reached
} catch (const std::exception& e) {
    std::cout << "Transaction rolled back: " << e.what() << std::endl;
}

// Method 3: Helper function with automatic commit/rollback
with_transaction(conn, [](database_transaction& txn) {
    (void)txn.execute("INSERT INTO logs (message) VALUES ('Transaction started')");
    // Automatically commits on success, rolls back on exception
});
```

### Savepoints

```cpp
database_transaction txn(conn);

(void)txn.execute("INSERT INTO users (name) VALUES ('Alice')");

{
    savepoint sp(conn, "sp1");
    (void)txn.execute("INSERT INTO users (name) VALUES ('Bob')");
    sp.rollback();  // Only Bob is rolled back
}

txn.commit();  // Alice is committed
```

### Connection Pooling

```cpp
// Create connection pool
database_pool::pool_config config{
    .connection_string = "host=localhost dbname=testdb user=testuser password=testpass",
    .min_connections = 5,
    .max_connections = 20,
    .acquire_timeout = std::chrono::seconds(10)
};

database_pool pool(config);

// Acquire connection (RAII - automatically returned to pool)
{
    auto conn = pool.acquire();  // Throws database_error if timeout
    auto result = conn->execute("SELECT * FROM users");
    query_result qr(result);
    // ... use connection
}  // Connection automatically returned to pool

// Thread-safe concurrent access
std::vector<std::thread> threads;
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&pool]() {
        try {
            auto conn = pool.acquire();
            auto result = conn->execute("SELECT 1");
            PQclear(result);
        } catch (const database_error& e) {
            std::cerr << "Pool error: " << e.what() << std::endl;
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
auto create_result = conn.execute(
    "CREATE OR REPLACE FUNCTION add_numbers(a INTEGER, b INTEGER) "
    "RETURNS INTEGER AS $$ "
    "BEGIN RETURN a + b; END; "
    "$$ LANGUAGE plpgsql"
);
PQclear(create_result);

// Call the function using execute_params
auto result = conn.execute_params("SELECT add_numbers($1, $2)", "10", "20");
query_result qr(result);
auto sum = qr.get<int>(0, 0);
if (sum) {
    std::cout << "Result: " << *sum << std::endl;  // 30
}
```

## Error Handling

Fenrir uses exception-based error handling with `database_error`:

```cpp
try {
    database_connection conn("host=localhost dbname=testdb user=testuser password=testpass");
    
    // All operations throw database_error on failure
    auto result = conn.execute("SELECT * FROM nonexistent_table");
    query_result qr(result);
    
} catch (const database_error& e) {
    // database_error inherits from std::runtime_error
    std::cerr << "Database error: " << e.what() << std::endl;
    
    // Contains full error information with source location
    // e.g., "Query execution failed: relation "nonexistent_table" does not exist"
}

// For operations that return PGresult*, wrap in query_result for RAII:
try {
    auto pg_result = conn.execute("SELECT * FROM users");
    query_result qr(pg_result);  // Automatically calls PQclear on destruction
    
    // Use result...
} catch (const database_error& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

## Type Safety

Query results use `std::optional` for nullable values:

```cpp
auto result = conn.execute("SELECT name, age FROM users WHERE id = 1");
query_result qr(result);

if (qr.row_count() > 0) {
    // Type-safe conversion with std::optional
    auto name = qr.get<std::string>(0, "name");
    auto age = qr.get<int>(0, "age");
    
    if (name && age) {
        std::cout << *name << " is " << *age << " years old" << std::endl;
    } else {
        std::cout << "NULL value encountered" << std::endl;
    }
}

// Or using value_or for defaults:
auto name = qr.get<std::string>(0, "name").value_or("Unknown");
auto age = qr.get<int>(0, "age").value_or(0);
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

## Testing

All tests use the database credentials: `testdb` / `testuser` / `testpass`

Make sure your PostgreSQL database is set up correctly before running tests (see [Database Setup](#database-setup) section above).

### Building Tests

```bash
cd fenrir
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Running Tests

```bash
# Run all tests with Catch2
ctest --verbose

# Or run individual test executables
./connection_test
./query_test
./transaction_test
./pool_test
```

### Test Files

- `tests/connection_test.cpp` - Connection management, queries, parameters
- `tests/query_test.cpp` - Query builder, result handling, type conversions
- `tests/transaction_test.cpp` - Transactions, savepoints, isolation levels
- `tests/pool_test.cpp` - Connection pooling, thread safety, statistics

## Examples

Complete working example demonstrating all features:

```bash
cd build
./usage_example
```

The example connects to the database and shows:
- Basic connection
- Query execution
- Transactions
- Connection pooling
- Error handling

## API Reference

### database_connection

Main connection class with RAII management.

**Construction:**
```cpp
// Connection string
database_connection conn("host=localhost dbname=testdb user=testuser password=testpass");

// Structured parameters
database_connection::connection_params params{
    .host = "localhost",
    .port = "5432",
    .database = "testdb",
    .user = "testuser",
    .password = "testpass"
};
database_connection conn(params);
```

**Methods:**
- `is_connected()` - Check connection status
- `execute(query)` - Execute simple query, returns `PGresult*`
- `execute_params(query, args...)` - Execute parameterized query, returns `PGresult*`
- `database_name()`, `user_name()`, `host()`, `port()` - Connection info
- `server_version()` - Get PostgreSQL server version
- `ping()` - Test connection liveness
- `reset()` - Reset connection
- `close()` - Close connection

**Note:** `execute()` and `execute_params()` return raw `PGresult*` pointers. Wrap them in `query_result` for automatic memory management, or manually call `PQclear()`.

### database_query

Query builder and execution class.

**Methods:**
- `select(columns)` - Start SELECT query
- `from(table)` - Specify table
- `where(condition)` - Add WHERE clause
- `order_by(column, asc=true)` - Add ORDER BY (ascending by default)
- `limit(n)` - Add LIMIT
- `offset(n)` - Add OFFSET
- `execute()` - Execute built query, returns `query_result`
- `raw(sql)` - Execute raw SQL, returns `query_result`
- `raw_params(sql, args...)` - Execute parameterized SQL, returns `query_result`
- `get_query()` - Get the built SQL string
- `reset()` - Reset query builder state

### query_result

RAII result set wrapper with automatic `PQclear()` on destruction.

**Construction:**
```cpp
auto pg_result = conn.execute("SELECT * FROM users");
query_result qr(pg_result);  // Takes ownership, will call PQclear()
```

**Methods:**
- `row_count()` - Number of rows returned
- `field_count()` - Number of columns/fields
- `affected_rows()` - Number of rows affected (INSERT/UPDATE/DELETE)
- `get<T>(row, col)` - Get typed value as `std::optional<T>`
- `get<T>(row, column_name)` - Get value by column name
- `is_null(row, col)` - Check if value is NULL
- `field_name(col)` - Get column name by index
- `field_index(name)` - Get column index by name
- `begin()`, `end()` - Iterator support for range-based for loops

### database_transaction

Transaction management with RAII and automatic rollback.

**Construction:**
```cpp
// Default: READ COMMITTED, READ WRITE
database_transaction txn(conn);

// With isolation level
database_transaction txn(conn, isolation_level::serializable);

// With access mode
database_transaction txn(conn, isolation_level::read_committed, access_mode::read_only);
```

**Methods:**
- `execute(sql)` - Execute query within transaction, returns `query_result`
- `commit()` - Commit transaction (throws if already finalized)
- `rollback()` - Rollback transaction (throws if already finalized)
- `is_committed()` - Check if committed
- `is_rolled_back()` - Check if rolled back

**Helper Function:**
```cpp
// Automatically commits on success, rolls back on exception
with_transaction(conn, [](database_transaction& txn) {
    txn.execute("INSERT INTO ...");
}, isolation_level::serializable);  // Optional isolation level
```

### savepoint

Savepoint management within transactions.

**Construction:**
```cpp
savepoint sp(conn, "savepoint_name");
```

**Methods:**
- `rollback()` - Rollback to this savepoint
- Destructor automatically releases savepoint if not rolled back

### database_pool

Thread-safe connection pool with automatic connection management.

**Configuration:**
```cpp
database_pool::pool_config config{
    .connection_string = "host=localhost dbname=testdb user=testuser password=testpass",
    .min_connections = 5,
    .max_connections = 20,
    .acquire_timeout = std::chrono::seconds(10)
};
```

**Methods:**
- `acquire()` - Acquire connection (throws `database_error` on timeout)
- `get_stats()` - Get pool statistics (`pool_stats` struct)

**pool_stats Structure:**
- `total_connections` - Total connections in pool
- `active_connections` - Currently in-use connections
- `available_connections` - Available for acquisition

### pooled_connection

RAII wrapper for pool connections, automatically returned on destruction.

**Usage:**
```cpp
auto conn = pool.acquire();
conn->execute("SELECT ...");  // Use like database_connection*
// Automatically returned to pool when conn goes out of scope
```

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

## Quick Reference

### Common Operations

```cpp
// Connect
database_connection conn("host=localhost dbname=testdb user=testuser password=testpass");

// Simple query
auto result = conn.execute("SELECT * FROM users");
query_result qr(result);

// Parameterized query
auto result = conn.execute_params("SELECT * FROM users WHERE age > $1", "18");

// Transaction
database_transaction txn(conn);
(void)txn.execute("INSERT INTO ...");
txn.commit();

// Query builder
database_query query(conn);
auto result = query.select("*").from("users").where("age > 18").execute();

// Connection pool
database_pool pool(config);
auto conn = pool.acquire();
conn->execute("SELECT 1");
```

### Test Database Credentials

- Database: `testdb`
- User: `testuser`
- Password: `testpass`
- Connection: `host=localhost port=5432 dbname=testdb user=testuser password=testpass`

Run `./setup_testdb.sh` to create the test database automatically.

## Support

For issues, questions, or contributions, please visit the GitHub repository.

---

Made with ❤️ using C++20 and PostgreSQL
