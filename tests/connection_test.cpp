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
            .database = "test_db",
            .user = "postgres",
            .password = "postgres"
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
    
    SECTION("Ping connection") {
        REQUIRE(conn.ping());
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
