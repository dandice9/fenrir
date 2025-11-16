#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "../src/fenrir.hpp"

using namespace fenrir;
using Catch::Matchers::ContainsSubstring;

// Test configuration - adjust these to match your PostgreSQL setup
const std::string TEST_CONNECTION_STRING = 
    "host=localhost port=5432 dbname=testdb user=testuser password=testpass";

TEST_CASE("database_connection - Construction and Connection", "[connection]") {
    
    SECTION("Connect with connection string") {
        REQUIRE_NOTHROW([&]() {
            database_connection conn(TEST_CONNECTION_STRING);
            REQUIRE(conn.is_connected());
            REQUIRE(conn.status() == connection_status::ok);
        }());
    }
    
    SECTION("Connect with connection params") {
        database_connection::connection_params params{
            .host = "localhost",
            .port = "5432",
            .database = "testdb",
            .user = "testuser",
            .password = "testpass"
        };
        
        REQUIRE_NOTHROW([&]() {
            database_connection conn(params);
            REQUIRE(conn.is_connected());
        }());
    }
    
    SECTION("Invalid connection throws") {
        REQUIRE_THROWS_AS([&]() {
            database_connection conn("host=invalid_host dbname=invalid_db");
        }(), database_error);
    }
}

TEST_CASE("database_connection - Move Semantics", "[connection]") {
    database_connection conn1(TEST_CONNECTION_STRING);
    REQUIRE(conn1.is_connected());
    
    SECTION("Move constructor") {
        database_connection conn2(std::move(conn1));
        REQUIRE(conn2.is_connected());
        REQUIRE_FALSE(conn1.is_connected());
    }
    
    SECTION("Move assignment") {
        database_connection conn2(TEST_CONNECTION_STRING);
        conn2 = std::move(conn1);
        REQUIRE(conn2.is_connected());
    }
}

TEST_CASE("database_connection - Query Execution", "[connection]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    SECTION("Simple query execution") {
        auto result = conn.execute("SELECT 1 AS test_value");
        REQUIRE(result != nullptr);
        PQclear(result);
    }
    
    SECTION("Create table and insert data") {
        // Create test table
        auto create_result = conn.execute(
            "CREATE TEMP TABLE test_users (id SERIAL PRIMARY KEY, name TEXT, age INT)"
        );
        REQUIRE(create_result != nullptr);
        PQclear(create_result);
        
        // Insert data
        auto insert_result = conn.execute(
            "INSERT INTO test_users (name, age) VALUES ('Alice', 30)"
        );
        REQUIRE(insert_result != nullptr);
        PQclear(insert_result);
    }
    
    SECTION("Invalid query throws error") {
        REQUIRE_THROWS_AS(conn.execute("INVALID SQL SYNTAX"), database_error);
    }
}

TEST_CASE("database_connection - Parameterized Queries", "[connection]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    // Setup test table
    auto create_result = conn.execute(
        "CREATE TEMP TABLE test_products (id SERIAL PRIMARY KEY, name TEXT, price DECIMAL)"
    );
    REQUIRE(create_result != nullptr);
    PQclear(create_result);
    
    SECTION("Execute with parameters") {
        auto result = conn.execute_params(
            "INSERT INTO test_products (name, price) VALUES ($1, $2)",
            "Widget", "19.99"
        );
        REQUIRE(result != nullptr);
        PQclear(result);
        
        // Verify insertion
        auto select_result = conn.execute("SELECT name, price FROM test_products WHERE name = 'Widget'");
        REQUIRE(select_result != nullptr);
        REQUIRE(PQntuples(select_result) == 1);
        PQclear(select_result);
    }
    
    SECTION("Execute with numeric parameters") {
        auto result = conn.execute_params(
            "INSERT INTO test_products (name, price) VALUES ($1, $2)",
            "Gadget", 42.5
        );
        REQUIRE(result != nullptr);
        PQclear(result);
    }
}

TEST_CASE("database_connection - Connection Info", "[connection]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    SECTION("Get database info") {
        REQUIRE_FALSE(conn.database_name().empty());
        REQUIRE_FALSE(conn.user_name().empty());
        REQUIRE_FALSE(conn.host().empty());
        REQUIRE_FALSE(conn.port().empty());
    }
}

TEST_CASE("database_connection - Connection Management", "[connection]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    SECTION("Reset connection") {
        REQUIRE(conn.is_connected());
        conn.reset();
        REQUIRE(conn.is_connected());
    }
    
    SECTION("Close connection") {
        REQUIRE(conn.is_connected());
        conn.close();
        REQUIRE_FALSE(conn.is_connected());
    }
}

TEST_CASE("database_connection - Error Handling", "[connection]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    SECTION("Execute on closed connection") {
        conn.close();
        REQUIRE_THROWS_AS(conn.execute("SELECT 1"), database_error);
    }
}

TEST_CASE("database_connection - Async Operations", "[connection][async]") {
    boost::asio::io_context ioc;
    database_connection conn(TEST_CONNECTION_STRING);
    
    SECTION("Async execute without io_context throws") {
        // Cannot test coroutine without running io_context
        // This is tested implicitly - calling async methods without io_context will throw at runtime
        REQUIRE(conn.get_io_context() == nullptr);
    }
    
    SECTION("Set io_context enables async operations") {
        conn.set_io_context(ioc);
        REQUIRE(conn.get_io_context() == &ioc);
    }
    
    SECTION("Async execute simple query") {
        conn.set_io_context(ioc);
        
        // Create test table
        auto result = conn.execute("CREATE TEMP TABLE async_test (id SERIAL, name TEXT)");
        PQclear(result);
        
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            // Test async_execute
            auto qr = co_await conn.async_execute("INSERT INTO async_test (name) VALUES ('Alice') RETURNING id");
            REQUIRE(qr.row_count() == 1);
            auto id = qr.get<int>(0, 0);
            REQUIRE(id.has_value());
            REQUIRE(id.value() > 0);
            
            // Verify data
            auto qr2 = co_await conn.async_execute("SELECT * FROM async_test WHERE id = " + std::to_string(id.value()));
            REQUIRE(qr2.row_count() == 1);
            auto name = qr2.get<std::string>(0, "name");
            REQUIRE(name.has_value());
            REQUIRE(name.value() == "Alice");
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
    }
    
    SECTION("Async execute with parameters") {
        conn.set_io_context(ioc);
        
        auto result = conn.execute("CREATE TEMP TABLE async_test (id SERIAL, name TEXT, age INT)");
        PQclear(result);
        
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            // Test async_execute_params
            auto qr = co_await conn.async_execute_params(
                "INSERT INTO async_test (name, age) VALUES ($1, $2) RETURNING id",
                "Bob", 30
            );
            REQUIRE(qr.row_count() == 1);
            
            // Query with params
            auto qr2 = co_await conn.async_execute_params(
                "SELECT * FROM async_test WHERE name = $1 AND age = $2",
                "Bob", 30
            );
            REQUIRE(qr2.row_count() == 1);
            auto name = qr2.get<std::string>(0, "name");
            auto age = qr2.get<int>(0, "age");
            REQUIRE(name.value() == "Bob");
            REQUIRE(age.value() == 30);
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
    }
    
    SECTION("Async prepared statements") {
        conn.set_io_context(ioc);
        
        auto result = conn.execute("CREATE TEMP TABLE async_test (id SERIAL, value INT)");
        PQclear(result);
        
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            // Prepare statement
            co_await conn.async_prepare("insert_value", "INSERT INTO async_test (value) VALUES ($1) RETURNING id");
            
            // Execute prepared statement multiple times
            auto qr1 = co_await conn.async_execute_prepared("insert_value", 100);
            REQUIRE(qr1.row_count() == 1);
            
            auto qr2 = co_await conn.async_execute_prepared("insert_value", 200);
            REQUIRE(qr2.row_count() == 1);
            
            // Verify both inserted
            auto qr3 = co_await conn.async_execute("SELECT COUNT(*) FROM async_test");
            auto count = qr3.get<int>(0, 0);
            REQUIRE(count.value() == 2);
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
    }
    
    SECTION("Mix sync and async operations") {
        conn.set_io_context(ioc);
        
        auto result = conn.execute("CREATE TEMP TABLE async_test (id SERIAL, data TEXT)");
        PQclear(result);
        
        // Sync insert
        auto sync_result = conn.execute_params("INSERT INTO async_test (data) VALUES ($1)", "sync");
        PQclear(sync_result);
        
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            // Async insert
            auto qr = co_await conn.async_execute_params(
                "INSERT INTO async_test (data) VALUES ($1) RETURNING id", 
                "async"
            );
            REQUIRE(qr.row_count() >= 1);  // Should have at least one row returned
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
        
        // Sync verify
        auto verify_result = conn.execute("SELECT COUNT(*) FROM async_test");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 2);
    }
}
