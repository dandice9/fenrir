#include "../src/fenrir.hpp"
#include <iostream>
#include <boost/asio.hpp>

using namespace fenrir;
namespace net = boost::asio;

// Example: Async stored procedure calls in a web server
net::awaitable<void> handle_user_creation(database_connection& conn) {
    try {
        // Create a stored procedure call
        database_stored_procedure proc(conn, "create_user");
        
        // Add parameters
        proc.add_param("username", "john_doe")
            .add_param("email", "john@example.com")
            .add_param("age", 30);
        
        // Execute asynchronously (non-blocking!)
        auto result = co_await proc.async_execute();
        
        std::cout << "User created! Rows affected: " << result.row_count() << "\n";
        
        if (result.row_count() > 0) {
            auto user_id = result.get<int>(0, "id");
            std::cout << "New user ID: " << user_id << "\n";
        }
        
    } catch (const database_error& e) {
        std::cerr << "Database error: " << e.what() << "\n";
        std::cerr << "SQL State: " << e.sql_state << "\n";
    }
}

// Example: Execute scalar function asynchronously
net::awaitable<void> get_user_count(database_connection& conn) {
    try {
        database_stored_procedure proc(conn, "get_total_users");
        
        // Execute as scalar (returns single value) - asynchronously
        auto count = co_await proc.async_execute_scalar<int>();
        
        if (count) {
            std::cout << "Total users: " << *count << "\n";
        } else {
            std::cout << "No result returned\n";
        }
        
    } catch (const database_error& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

// Example: Multiple concurrent stored procedure calls
net::awaitable<void> process_batch_operations(database_connection& conn) {
    std::cout << "Starting batch operations...\n";
    
    // Operation 1: Get user count
    auto task1 = [&conn]() -> net::awaitable<void> {
        database_stored_procedure proc(conn, "get_total_users");
        auto count = co_await proc.async_execute_scalar<int>();
        std::cout << "  User count: " << (count ? *count : 0) << "\n";
    };
    
    // Operation 2: Get active sessions
    auto task2 = [&conn]() -> net::awaitable<void> {
        database_stored_procedure proc(conn, "get_active_sessions");
        auto result = co_await proc.async_execute();
        std::cout << "  Active sessions: " << result.row_count() << "\n";
    };
    
    // Operation 3: Clean old records
    auto task3 = [&conn]() -> net::awaitable<void> {
        database_stored_procedure proc(conn, "cleanup_old_records");
        proc.add_param("days_old", 30);
        auto result = co_await proc.async_execute();
        std::cout << "  Cleaned records: " << result.row_count() << "\n";
    };
    
    // Execute all concurrently (if using multiple connections)
    // Note: Single connection executes sequentially
    co_await task1();
    co_await task2();
    co_await task3();
    
    std::cout << "Batch operations complete!\n";
}

// Example: Using with Wolf web server
#ifdef USING_WOLF_WEBSERVER
#include "../../wolf/src/wolf.hpp"

net::awaitable<wolf::http_response> create_user_handler(
    const wolf::http_request& req, 
    database_connection& db) {
    
    try {
        // Parse JSON body
        auto body = req.get_json_body();
        auto obj = body.as_object();
        
        std::string username = obj["username"].as_string().c_str();
        std::string email = obj["email"].as_string().c_str();
        int age = obj["age"].as_int64();
        
        // Call stored procedure asynchronously
        database_stored_procedure proc(db, "create_user");
        proc.add_param("username", username)
            .add_param("email", email)
            .add_param("age", age);
        
        auto result = co_await proc.async_execute();
        
        if (result.row_count() > 0) {
            auto user_id = result.get<int>(0, "id");
            
            boost::json::object response = {
                {"success", true},
                {"user_id", user_id},
                {"message", "User created successfully"}
            };
            
            co_return wolf::http_response(201).json(response);
        } else {
            co_return wolf::http_response(500).json({
                {"success", false},
                {"message", "Failed to create user"}
            });
        }
        
    } catch (const database_error& e) {
        co_return wolf::http_response(500).json({
            {"success", false},
            {"error", e.what()},
            {"sql_state", e.sql_state}
        });
    }
}
#endif

// Standalone example
int main() {
    net::io_context ioc;
    
    try {
        // Connect to database
        database_connection conn("host=localhost dbname=testdb user=postgres password=secret");
        
        if (!conn.is_connected()) {
            std::cerr << "Failed to connect to database\n";
            return 1;
        }
        
        std::cout << "Connected to database!\n\n";
        
        // Run async operations
        net::co_spawn(ioc, handle_user_creation(conn), net::detached);
        net::co_spawn(ioc, get_user_count(conn), net::detached);
        net::co_spawn(ioc, process_batch_operations(conn), net::detached);
        
        // Run the event loop
        ioc.run();
        
        std::cout << "\nAll operations completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
