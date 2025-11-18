#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "../src/fenrir.hpp"
#include <boost/asio.hpp>
#include <thread>
#include <chrono>

using namespace fenrir;
namespace net = boost::asio;

// Test database credentials
constexpr const char* TEST_CONN_STRING = "host=localhost port=5432 dbname=testdb user=testuser password=testpass";

// Test fixture for stored procedures
struct StoredProcedureFixture {
    StoredProcedureFixture() {
        setup_test_functions();
    }
    
    ~StoredProcedureFixture() {
        cleanup_test_functions();
    }
    
    void setup_test_functions() {
        database_connection conn(TEST_CONN_STRING);
        
        // Drop existing test functions
        auto drop1 = conn.execute("DROP FUNCTION IF EXISTS test_add_numbers(INTEGER, INTEGER)");
        PQclear(drop1);
        
        auto drop2 = conn.execute("DROP FUNCTION IF EXISTS test_get_constant()");
        PQclear(drop2);
        
        auto drop3 = conn.execute("DROP FUNCTION IF EXISTS test_get_user_count()");
        PQclear(drop3);
        
        auto drop4 = conn.execute("DROP FUNCTION IF EXISTS test_create_user(TEXT, TEXT, INTEGER)");
        PQclear(drop4);
        
        auto drop5 = conn.execute("DROP FUNCTION IF EXISTS test_return_empty()");
        PQclear(drop5);
        
        auto drop6 = conn.execute("DROP FUNCTION IF EXISTS test_types(INTEGER, TEXT, BOOLEAN)");
        PQclear(drop6);
        
        // Create test functions
        auto create1 = conn.execute(
            "CREATE OR REPLACE FUNCTION test_add_numbers(a INTEGER, b INTEGER) "
            "RETURNS INTEGER AS $$ "
            "BEGIN RETURN a + b; END; "
            "$$ LANGUAGE plpgsql"
        );
        PQclear(create1);
        
        auto create2 = conn.execute(
            "CREATE OR REPLACE FUNCTION test_get_constant() "
            "RETURNS INTEGER AS $$ "
            "BEGIN RETURN 42; END; "
            "$$ LANGUAGE plpgsql"
        );
        PQclear(create2);
        
        auto create3 = conn.execute(
            "CREATE OR REPLACE FUNCTION test_get_user_count() "
            "RETURNS INTEGER AS $$ "
            "BEGIN RETURN 100; END; "
            "$$ LANGUAGE plpgsql"
        );
        PQclear(create3);
        
        auto create4 = conn.execute(
            "CREATE OR REPLACE FUNCTION test_create_user(p_username TEXT, p_email TEXT, p_age INTEGER) "
            "RETURNS TABLE(id INTEGER, username TEXT, email TEXT, age INTEGER) AS $$ "
            "BEGIN "
            "  RETURN QUERY SELECT 999, p_username, p_email, p_age; "
            "END; "
            "$$ LANGUAGE plpgsql"
        );
        PQclear(create4);
        
        auto create5 = conn.execute(
            "CREATE OR REPLACE FUNCTION test_return_empty() "
            "RETURNS TABLE(value INTEGER) AS $$ "
            "BEGIN "
            "  RETURN; "
            "END; "
            "$$ LANGUAGE plpgsql"
        );
        PQclear(create5);
        
        auto create6 = conn.execute(
            "CREATE OR REPLACE FUNCTION test_types(p_int INTEGER, p_text TEXT, p_bool BOOLEAN) "
            "RETURNS TABLE(col_int INTEGER, col_text TEXT, col_bool BOOLEAN) AS $$ "
            "BEGIN "
            "  RETURN QUERY SELECT p_int, p_text, p_bool; "
            "END; "
            "$$ LANGUAGE plpgsql"
        );
        PQclear(create6);
    }
    
    void cleanup_test_functions() {
        try {
            database_connection conn(TEST_CONN_STRING);
            
            auto drop1 = conn.execute("DROP FUNCTION IF EXISTS test_add_numbers(INTEGER, INTEGER)");
            PQclear(drop1);
            
            auto drop2 = conn.execute("DROP FUNCTION IF EXISTS test_get_constant()");
            PQclear(drop2);
            
            auto drop3 = conn.execute("DROP FUNCTION IF EXISTS test_get_user_count()");
            PQclear(drop3);
            
            auto drop4 = conn.execute("DROP FUNCTION IF EXISTS test_create_user(TEXT, TEXT, INTEGER)");
            PQclear(drop4);
            
            auto drop5 = conn.execute("DROP FUNCTION IF EXISTS test_return_empty()");
            PQclear(drop5);
            
            auto drop6 = conn.execute("DROP FUNCTION IF EXISTS test_types(INTEGER, TEXT, BOOLEAN)");
            PQclear(drop6);
        } catch (...) {
            // Ignore cleanup errors
        }
    }
};

TEST_CASE("Async Stored Procedures - Setup", "[stored_procedure][async][setup]") {
    database_connection conn(TEST_CONN_STRING);
    REQUIRE(conn.is_connected());
    
    // Drop existing test functions
    auto drop1 = conn.execute("DROP FUNCTION IF EXISTS test_add_numbers(INTEGER, INTEGER)");
    PQclear(drop1);
    
    auto drop2 = conn.execute("DROP FUNCTION IF EXISTS test_get_constant()");
    PQclear(drop2);
    
    auto drop3 = conn.execute("DROP FUNCTION IF EXISTS test_get_user_count()");
    PQclear(drop3);
    
    auto drop4 = conn.execute("DROP FUNCTION IF EXISTS test_create_user(TEXT, TEXT, INTEGER)");
    PQclear(drop4);
    
    // Create test functions
    auto create1 = conn.execute(
        "CREATE OR REPLACE FUNCTION test_add_numbers(a INTEGER, b INTEGER) "
        "RETURNS INTEGER AS $$ "
        "BEGIN RETURN a + b; END; "
        "$$ LANGUAGE plpgsql"
    );
    PQclear(create1);
    
    auto create2 = conn.execute(
        "CREATE OR REPLACE FUNCTION test_get_constant() "
        "RETURNS INTEGER AS $$ "
        "BEGIN RETURN 42; END; "
        "$$ LANGUAGE plpgsql"
    );
    PQclear(create2);
    
    auto create3 = conn.execute(
        "CREATE OR REPLACE FUNCTION test_get_user_count() "
        "RETURNS INTEGER AS $$ "
        "BEGIN RETURN 100; END; "
        "$$ LANGUAGE plpgsql"
    );
    PQclear(create3);
    
    auto create4 = conn.execute(
        "CREATE OR REPLACE FUNCTION test_create_user(p_username TEXT, p_email TEXT, p_age INTEGER) "
        "RETURNS TABLE(id INTEGER, username TEXT, email TEXT, age INTEGER) AS $$ "
        "BEGIN "
        "  RETURN QUERY SELECT 999, p_username, p_email, p_age; "
        "END; "
        "$$ LANGUAGE plpgsql"
    );
    PQclear(create4);
}

TEST_CASE("Async Stored Procedures - Basic Execution", "[stored_procedure][async]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Execute simple function without parameters") {
        bool test_completed = false;
        std::exception_ptr eptr;
        
        auto test_func = [&]() -> net::awaitable<void> {
            try {
                database_stored_procedure proc(conn, "test_get_constant");
                
                auto result = co_await proc.async_execute();
                
                REQUIRE(result.row_count() > 0);
                auto value = result.get<int>(0, 0);
                REQUIRE(value.has_value());
                REQUIRE(value.value() == 42);
                
                test_completed = true;
            } catch (...) {
                eptr = std::current_exception();
            }
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        if (eptr) {
            std::rethrow_exception(eptr);
        }
        REQUIRE(test_completed);
    }
    
    SECTION("Execute function with parameters") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure proc(conn, "test_add_numbers");
            proc.add_param("a", 10)
                .add_param("b", 20);
            
            auto result = co_await proc.async_execute();
            
            REQUIRE(result.row_count() > 0);
            auto sum = result.get<int>(0, 0);
            REQUIRE(sum.has_value());
            REQUIRE(sum.value() == 30);
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
    
    SECTION("Execute function with string parameters") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure proc(conn, "test_create_user");
            proc.add_param("p_username", "alice")
                .add_param("p_email", "alice@example.com")
                .add_param("p_age", 28);
            
            auto result = co_await proc.async_execute();
            
            REQUIRE(result.row_count() > 0);
            auto id = result.get<int>(0, "id");
            auto username = result.get<std::string>(0, "username");
            auto email = result.get<std::string>(0, "email");
            auto age = result.get<int>(0, "age");
            
            REQUIRE(id.has_value());
            REQUIRE(id.value() == 999);
            REQUIRE(username.has_value());
            REQUIRE(username.value() == "alice");
            REQUIRE(email.has_value());
            REQUIRE(email.value() == "alice@example.com");
            REQUIRE(age.has_value());
            REQUIRE(age.value() == 28);
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
}

TEST_CASE("Async Stored Procedures - Scalar Execution", "[stored_procedure][async][scalar]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Execute scalar without parameters") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure proc(conn, "test_get_constant");
            
            auto value = co_await proc.async_execute_scalar<int>();
            
            REQUIRE(value.has_value());
            REQUIRE(value.value() == 42);
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
    
    SECTION("Execute scalar with parameters") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure proc(conn, "test_add_numbers");
            proc.add_param("a", 100)
                .add_param("b", 200);
            
            auto sum = co_await proc.async_execute_scalar<int>();
            
            REQUIRE(sum.has_value());
            REQUIRE(sum.value() == 300);
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
    
    SECTION("Execute scalar returns nullopt for empty result") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            // Create a function that returns empty result
            auto create = conn.execute(
                "CREATE OR REPLACE FUNCTION test_return_empty() "
                "RETURNS TABLE(val INTEGER) AS $$ "
                "BEGIN "
                "  RETURN; "
                "END; "
                "$$ LANGUAGE plpgsql"
            );
            PQclear(create);
            
            database_stored_procedure proc(conn, "test_return_empty");
            auto value = co_await proc.async_execute_scalar<int>();
            
            REQUIRE_FALSE(value.has_value());
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
}

TEST_CASE("Async Stored Procedures - Sequential Execution", "[stored_procedure][async][sequential]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Multiple sequential async calls") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            // First call
            database_stored_procedure proc1(conn, "test_add_numbers");
            proc1.add_param("a", 5).add_param("b", 10);
            auto result1 = co_await proc1.async_execute();
            auto sum1 = result1.get<int>(0, 0);
            REQUIRE(sum1.value() == 15);
            
            // Second call
            database_stored_procedure proc2(conn, "test_add_numbers");
            proc2.add_param("a", 20).add_param("b", 30);
            auto result2 = co_await proc2.async_execute();
            auto sum2 = result2.get<int>(0, 0);
            REQUIRE(sum2.value() == 50);
            
            // Third call - scalar
            database_stored_procedure proc3(conn, "test_get_constant");
            auto value = co_await proc3.async_execute_scalar<int>();
            REQUIRE(value.value() == 42);
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
}

TEST_CASE("Async Stored Procedures - Parameter Management", "[stored_procedure][async][params]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Parameter reuse with clear") {
        bool test_completed = false;
        
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure proc(conn, "test_add_numbers");
            
            // First execution
            proc.add_param("a", 1).add_param("b", 2);
            auto result1 = co_await proc.async_execute();
            REQUIRE(result1.get<int>(0, 0).value() == 3);
            
            // Clear and reuse
            proc.clear_params();
            proc.add_param("a", 10).add_param("b", 20);
            auto result2 = co_await proc.async_execute();
            REQUIRE(result2.get<int>(0, 0).value() == 30);
            
            test_completed = true;
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(test_completed);
    }
    
    SECTION("Various parameter types") {
        bool test_completed = false;
        std::exception_ptr eptr;
        
        auto test_func = [&]() -> net::awaitable<void> {
            try {
                // test_types function is already created by fixture
                database_stored_procedure proc(conn, "test_types");
                proc.add_param("p_int", 42)
                    .add_param("p_str", "hello")
                    .add_param("p_bool", true);
                
                auto result = co_await proc.async_execute();
                
                REQUIRE(result.get<int>(0, "col_int").value() == 42);
                REQUIRE(result.get<std::string>(0, "col_text").value() == "hello");
                REQUIRE(result.get<bool>(0, "col_bool").value() == true);
                
                test_completed = true;
            } catch (...) {
                eptr = std::current_exception();
            }
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        if (eptr) {
            std::rethrow_exception(eptr);
        }
        REQUIRE(test_completed);
    }
}

TEST_CASE("Async Stored Procedures - Error Handling", "[stored_procedure][async][error]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Non-existent function throws error") {
        bool error_caught = false;
        std::exception_ptr eptr;
        
        auto test_func = [&]() -> net::awaitable<void> {
            try {
                database_stored_procedure proc(conn, "nonexistent_function");
                auto result = co_await proc.async_execute();
                FAIL("Should have thrown database_error");
            } catch (const database_error& e) {
                error_caught = true;
                REQUIRE(std::string(e.what()).find("does not exist") != std::string::npos);
            } catch (...) {
                eptr = std::current_exception();
            }
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        if (eptr) {
            std::rethrow_exception(eptr);
        }
        REQUIRE(error_caught);
    }
    
    SECTION("Wrong parameter count throws error") {
        bool error_caught = false;
        std::exception_ptr eptr;
        
        auto test_func = [&]() -> net::awaitable<void> {
            try {
                database_stored_procedure proc(conn, "test_add_numbers");
                proc.add_param("a", 10);  // Missing second parameter
                auto result = co_await proc.async_execute();
                FAIL("Should have thrown database_error");
            } catch (const database_error& e) {
                error_caught = true;
            } catch (...) {
                eptr = std::current_exception();
            }
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        if (eptr) {
            std::rethrow_exception(eptr);
        }
        REQUIRE(error_caught);
    }
}

TEST_CASE("Async Stored Procedures - Sync vs Async Comparison", "[stored_procedure][async][sync]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Same result from sync and async execution") {
        int sync_result = 0;
        int async_result = 0;
        
        // Sync execution
        database_stored_procedure sync_proc(conn, "test_add_numbers");
        sync_proc.add_param("a", 50).add_param("b", 75);
        auto sync_res = sync_proc.execute();
        sync_result = sync_res.get<int>(0, 0).value();
        
        // Async execution
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure async_proc(conn, "test_add_numbers");
            async_proc.add_param("a", 50).add_param("b", 75);
            auto async_res = co_await async_proc.async_execute();
            async_result = async_res.get<int>(0, 0).value();
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(sync_result == async_result);
        REQUIRE(sync_result == 125);
    }
    
    SECTION("Same scalar result from sync and async") {
        auto sync_value = std::optional<int>{};
        auto async_value = std::optional<int>{};
        
        // Sync scalar
        database_stored_procedure sync_proc(conn, "test_get_constant");
        sync_value = sync_proc.execute_scalar<int>();
        
        // Async scalar
        auto test_func = [&]() -> net::awaitable<void> {
            database_stored_procedure async_proc(conn, "test_get_constant");
            async_value = co_await async_proc.async_execute_scalar<int>();
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(sync_value.has_value());
        REQUIRE(async_value.has_value());
        REQUIRE(sync_value.value() == async_value.value());
        REQUIRE(sync_value.value() == 42);
    }
}

TEST_CASE("Async Stored Procedures - Performance", "[stored_procedure][async][performance]") {
    StoredProcedureFixture fixture;
    net::io_context ioc;
    database_connection conn(TEST_CONN_STRING);
    conn.set_io_context(ioc);
    
    SECTION("Multiple async calls complete successfully") {
        int completed_count = 0;
        const int num_calls = 10;
        
        auto test_func = [&]() -> net::awaitable<void> {
            for (int i = 0; i < num_calls; ++i) {
                database_stored_procedure proc(conn, "test_add_numbers");
                proc.add_param("a", i).add_param("b", i * 2);
                
                auto result = co_await proc.async_execute();
                auto sum = result.get<int>(0, 0);
                
                REQUIRE(sum.has_value());
                REQUIRE(sum.value() == i + (i * 2));
                
                completed_count++;
            }
        };
        
        net::co_spawn(ioc, test_func(), net::detached);
        ioc.run();
        
        REQUIRE(completed_count == num_calls);
    }
}

TEST_CASE("Async Stored Procedures - Cleanup", "[stored_procedure][async][cleanup]") {
    database_connection conn(TEST_CONN_STRING);
    
    // Drop test functions
    auto drop1 = conn.execute("DROP FUNCTION IF EXISTS test_add_numbers(INTEGER, INTEGER)");
    PQclear(drop1);
    
    auto drop2 = conn.execute("DROP FUNCTION IF EXISTS test_get_constant()");
    PQclear(drop2);
    
    auto drop3 = conn.execute("DROP FUNCTION IF EXISTS test_get_user_count()");
    PQclear(drop3);
    
    auto drop4 = conn.execute("DROP FUNCTION IF EXISTS test_create_user(TEXT, TEXT, INTEGER)");
    PQclear(drop4);
    
    auto drop5 = conn.execute("DROP FUNCTION IF EXISTS test_return_empty()");
    PQclear(drop5);
    
    auto drop6 = conn.execute("DROP FUNCTION IF EXISTS test_types(INTEGER, TEXT, BOOLEAN)");
    PQclear(drop6);
    
    REQUIRE(true);  // Cleanup completed
}
