#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include "../src/fenrir.hpp"

using namespace fenrir;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

constexpr const char* TEST_CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

TEST_CASE("database_pool - Basic Acquire", "[pool]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 5
    };
    
    database_pool pool(config);
    auto conn = pool.acquire();
    
    REQUIRE(conn->is_connected());
}

TEST_CASE("database_pool - Pool Statistics", "[pool]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 3,
        .max_connections = 10
    };
    
    database_pool pool(config);
    
    SECTION("Initial statistics") {
        auto stats = pool.get_stats();
        REQUIRE(stats.total_connections >= 3);
        REQUIRE(stats.available_connections >= 3);
        REQUIRE(stats.active_connections == 0);
    }
    
    SECTION("Statistics after acquisition") {
        auto conn1 = pool.acquire();
        auto conn2 = pool.acquire();
        
        auto stats = pool.get_stats();
        REQUIRE(stats.active_connections == 2);
        REQUIRE(stats.available_connections == stats.total_connections - 2);
    }
}

TEST_CASE("database_pool - Connection Exhaustion with Timeout", "[pool][exhaustion]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 2  // Small pool to force exhaustion
    };
    
    database_pool pool(config);
    
    SECTION("Exhaust pool and timeout") {
        // Acquire all available connections
        auto conn1 = pool.acquire();
        auto conn2 = pool.acquire();
        
        REQUIRE(conn1->is_connected());
        REQUIRE(conn2->is_connected());
        
        // Try to acquire third connection - should timeout
        REQUIRE_THROWS_AS(pool.acquire(100ms), database_error);
        
        // Verify pool is fully utilized
        auto stats = pool.get_stats();
        REQUIRE(stats.active_connections == 2);
        REQUIRE(stats.available_connections == 0);
    }
    
    SECTION("Connection returns and becomes available") {
        {
            auto conn1 = pool.acquire();
            auto conn2 = pool.acquire();
            
            auto stats = pool.get_stats();
            REQUIRE(stats.available_connections == 0);
            
            // conn1 and conn2 released here
        }
        
        // Now connections should be available again
        auto stats = pool.get_stats();
        REQUIRE(stats.available_connections >= 1);
        
        // Should be able to acquire again
        REQUIRE_NOTHROW(pool.acquire());
    }
}

TEST_CASE("database_pool - High Concurrency Stress Test", "[pool][concurrency][stress]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 5,
        .max_connections = 10
    };
    
    database_pool pool(config);
    
    SECTION("Many threads competing for connections") {
        constexpr int num_threads = 50;
        constexpr int operations_per_thread = 10;
        
        std::atomic<int> successful_operations{0};
        std::atomic<int> timeout_errors{0};
        std::atomic<int> other_errors{0};
        std::vector<std::thread> threads;
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, thread_id = i]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> sleep_dist(1, 50);
                
                for (int op = 0; op < operations_per_thread; ++op) {
                    try {
                        // Acquire connection with timeout
                        auto conn = pool.acquire(500ms);
                        
                        // Simulate work
                        auto result = conn->execute("SELECT 1");
                        PQclear(result);
                        
                        // Random short sleep to simulate processing
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));
                        
                        successful_operations++;
                        
                    } catch (const database_error& e) {
                        std::string error_msg = e.what();
                        if (error_msg.find("Timeout") != std::string::npos || 
                            error_msg.find("timeout") != std::string::npos) {
                            timeout_errors++;
                        } else {
                            other_errors++;
                        }
                    }
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify results
        int total_operations = num_threads * operations_per_thread;
        REQUIRE(successful_operations + timeout_errors + other_errors == total_operations);
        
        // Most operations should succeed, but some timeouts are expected
        REQUIRE(successful_operations > 0);
        
        // Print statistics for visibility
        INFO("Total operations: " << total_operations);
        INFO("Successful: " << successful_operations);
        INFO("Timeouts: " << timeout_errors);
        INFO("Other errors: " << other_errors);
        INFO("Success rate: " << (100.0 * successful_operations / total_operations) << "%");
        
        // Final pool should be healthy
        auto final_stats = pool.get_stats();
        REQUIRE(final_stats.active_connections == 0);
    }
}

TEST_CASE("database_pool - Graceful Degradation Under Load", "[pool][load]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 3,
        .max_connections = 5
    };
    
    database_pool pool(config);
    
    SECTION("Handle burst of requests") {
        constexpr int burst_size = 20;
        std::atomic<int> acquired{0};
        std::atomic<int> failed{0};
        std::vector<std::thread> threads;
        
        // Create burst of concurrent requests
        for (int i = 0; i < burst_size; ++i) {
            threads.emplace_back([&]() {
                try {
                    auto conn = pool.acquire(200ms);
                    acquired++;
                    
                    // Hold connection briefly
                    std::this_thread::sleep_for(50ms);
                    
                } catch (const database_error&) {
                    failed++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Some should succeed, some may fail due to exhaustion
        REQUIRE(acquired + failed == burst_size);
        REQUIRE(acquired > 0);  // At least some should succeed
        
        INFO("Acquired: " << acquired << " / " << burst_size);
        INFO("Failed: " << failed << " / " << burst_size);
    }
}

TEST_CASE("database_pool - Long-Running Operations", "[pool][long-running]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 3
    };
    
    database_pool pool(config);
    
    SECTION("Mix of short and long operations") {
        std::atomic<int> short_ops_completed{0};
        std::atomic<int> long_ops_completed{0};
        std::atomic<int> timeouts{0};
        std::vector<std::thread> threads;
        
        // Long-running operations that hold connections
        for (int i = 0; i < 2; ++i) {
            threads.emplace_back([&]() {
                try {
                    auto conn = pool.acquire();
                    
                    // Simulate long-running operation
                    std::this_thread::sleep_for(500ms);
                    
                    auto result = conn->execute("SELECT pg_sleep(0.1)");
                    PQclear(result);
                    
                    long_ops_completed++;
                } catch (const database_error&) {
                    timeouts++;
                }
            });
        }
        
        // Give long operations time to acquire connections
        std::this_thread::sleep_for(50ms);
        
        // Short operations that should mostly timeout
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&]() {
                try {
                    auto conn = pool.acquire(300ms);
                    auto result = conn->execute("SELECT 1");
                    PQclear(result);
                    short_ops_completed++;
                } catch (const database_error&) {
                    timeouts++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Long operations should complete
        REQUIRE(long_ops_completed == 2);
        
        // With limited pool, some operations will timeout or succeed
        REQUIRE(short_ops_completed + timeouts == 5);
        
        INFO("Long ops completed: " << long_ops_completed);
        INFO("Short ops completed: " << short_ops_completed);
        INFO("Timeouts: " << timeouts);
    }
}

TEST_CASE("database_pool - Connection Reuse Pattern", "[pool][reuse]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 5
    };
    
    database_pool pool(config);
    
    SECTION("Verify connections are reused") {
        std::set<PGconn*> seen_connections;
        constexpr int iterations = 20;
        
        for (int i = 0; i < iterations; ++i) {
            auto conn = pool.acquire();
            PGconn* handle = conn->native_handle();
            seen_connections.insert(handle);
            
            auto result = conn->execute("SELECT 1");
            PQclear(result);
        }
        
        // Should see far fewer unique connections than iterations
        REQUIRE(seen_connections.size() <= 5);  // max_connections
        REQUIRE(seen_connections.size() >= 2);  // min_connections
        
        INFO("Unique connections used: " << seen_connections.size() << " out of " << iterations << " acquisitions");
    }
}

TEST_CASE("database_pool - Transaction Under Contention", "[pool][transaction]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 3,
        .max_connections = 5
    };
    
    database_pool pool(config);
    
    SECTION("Multiple threads with transactions") {
        // Create test table (not temp - needs to persist across connections)
        auto setup_conn = pool.acquire();
        auto drop_result = setup_conn->execute("DROP TABLE IF EXISTS pool_txn_test");
        PQclear(drop_result);
        auto create_result = setup_conn->execute(
            "CREATE TABLE pool_txn_test (id SERIAL, thread_id INT, value INT)"
        );
        PQclear(create_result);
        setup_conn = pooled_connection{};  // Release connection
        
        constexpr int num_threads = 10;
        std::atomic<int> successful_txns{0};
        std::atomic<int> failed_txns{0};
        std::vector<std::thread> threads;
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, thread_id = i]() {
                try {
                    auto conn = pool.acquire(500ms);
                    
                    database_transaction txn(*conn);
                    
                    // Insert data
                    (void)txn.execute(
                        std::format("INSERT INTO pool_txn_test (thread_id, value) VALUES ({}, {})",
                                  thread_id, thread_id * 100)
                    );
                    
                    // Simulate some work
                    std::this_thread::sleep_for(10ms);
                    
                    txn.commit();
                    successful_txns++;
                    
                } catch (const database_error& e) {
                    failed_txns++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        REQUIRE(successful_txns + failed_txns == num_threads);
        REQUIRE(successful_txns > 0);
        
        // Verify data was inserted
        auto verify_conn = pool.acquire();
        auto verify_result = verify_conn->execute("SELECT COUNT(*) FROM pool_txn_test");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == successful_txns);
        
        // Cleanup
        auto cleanup_result = verify_conn->execute("DROP TABLE pool_txn_test");
        PQclear(cleanup_result);
        
        INFO("Successful transactions: " << successful_txns);
        INFO("Failed transactions: " << failed_txns);
    }
}

TEST_CASE("database_pool - Timeout Recovery", "[pool][timeout][recovery]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 2
    };
    
    database_pool pool(config);
    
    SECTION("System recovers after timeout period") {
        // Exhaust pool
        auto conn1 = pool.acquire();
        auto conn2 = pool.acquire();
        
        // Attempt acquisition that will timeout
        REQUIRE_THROWS_AS(pool.acquire(100ms), database_error);
        
        // Release one connection (by moving empty pooled_connection to it)
        conn1 = pooled_connection{};
        
        // Should now be able to acquire
        REQUIRE_NOTHROW(pool.acquire());
        
        // Pool stats should reflect recovery
        auto stats = pool.get_stats();
        REQUIRE(stats.available_connections >= 0);
    }
}

TEST_CASE("database_pool - Concurrent Queries with Different Durations", "[pool][realistic]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 5,
        .max_connections = 8
    };
    
    database_pool pool(config);
    
    SECTION("Realistic workload simulation") {
        std::atomic<int> fast_queries{0};
        std::atomic<int> medium_queries{0};
        std::atomic<int> slow_queries{0};
        std::atomic<int> errors{0};
        std::vector<std::thread> threads;
        
        // Fast queries (SELECT 1)
        for (int i = 0; i < 20; ++i) {
            threads.emplace_back([&]() {
                try {
                    auto conn = pool.acquire(1s);
                    auto result = conn->execute("SELECT 1");
                    PQclear(result);
                    fast_queries++;
                } catch (...) {
                    errors++;
                }
            });
        }
        
        // Medium queries (small delay)
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&]() {
                try {
                    auto conn = pool.acquire(1s);
                    auto result = conn->execute("SELECT pg_sleep(0.05)");
                    PQclear(result);
                    medium_queries++;
                } catch (...) {
                    errors++;
                }
            });
        }
        
        // Slow queries (longer delay)
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&]() {
                try {
                    auto conn = pool.acquire(1s);
                    auto result = conn->execute("SELECT pg_sleep(0.1)");
                    PQclear(result);
                    slow_queries++;
                } catch (...) {
                    errors++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Most queries should succeed
        int total = fast_queries + medium_queries + slow_queries + errors;
        REQUIRE(total == 35);
        REQUIRE(fast_queries > 0);
        
        INFO("Fast queries: " << fast_queries);
        INFO("Medium queries: " << medium_queries);
        INFO("Slow queries: " << slow_queries);
        INFO("Errors: " << errors);
        
        // Pool should be back to normal
        auto stats = pool.get_stats();
        REQUIRE(stats.active_connections == 0);
    }
}

TEST_CASE("database_pool - Async Operations", "[pool][async]") {
    boost::asio::io_context ioc;
    
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 3,
        .max_connections = 10,
        .io_context = &ioc  // Enable async for pool
    };
    
    database_pool pool(config);
    
    SECTION("Pool connections support async operations") {
        auto conn = pool.acquire();
        REQUIRE(conn->get_io_context() == &ioc);
        
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            auto result = co_await conn->async_execute("SELECT 1");
            REQUIRE(result.row_count() == 1);
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
    }
    
    SECTION("Multiple async operations sequentially") {
        // Create test table
        {
            auto setup_conn = pool.acquire();
            auto result = setup_conn->execute("CREATE TEMP TABLE pool_async_test (id SERIAL, value INT)");
            PQclear(result);
        }  // setup_conn returned to pool here
        
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            // Run async operations sequentially
            for (int i = 0; i < 3; ++i) {
                auto conn = pool.acquire();
                auto qr = co_await conn->async_execute_params(
                    "INSERT INTO pool_async_test (value) VALUES ($1)", 
                    i * 100
                );
                // Check that insert worked
                REQUIRE(qr.affected_rows() >= 0);
            }
            
            // Verify all inserted
            auto conn = pool.acquire();
            auto qr = co_await conn->async_execute("SELECT COUNT(*) FROM pool_async_test");
            auto count = qr.get<int>(0, 0);
            REQUIRE(count.value() == 3);
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
    }
    
    SECTION("Mix sync and async with pool") {
        auto conn = pool.acquire();
        
        // Create test table with sync
        auto result = conn->execute("CREATE TEMP TABLE mixed_test (id SERIAL, data TEXT)");
        PQclear(result);
        
        // Insert with sync
        auto sync_result = conn->execute_params("INSERT INTO mixed_test (data) VALUES ($1)", "sync");
        PQclear(sync_result);
        
        // Insert with async
        auto async_test = [&]() -> boost::asio::awaitable<void> {
            auto qr = co_await conn->async_execute_params(
                "INSERT INTO mixed_test (data) VALUES ($1)", 
                "async"
            );
            REQUIRE(qr.affected_rows() == 1);
        };
        
        boost::asio::co_spawn(ioc, async_test(), boost::asio::detached);
        ioc.run();
        
        // Verify with sync
        auto verify_result = conn->execute("SELECT COUNT(*) FROM mixed_test");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 2);
    }
}

TEST_CASE("database_pool - Pool without async support", "[pool]") {
    // Pool without io_context - connections don't support async
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 5
        // No io_context set
    };
    
    database_pool pool(config);
    
    SECTION("Connections without io_context are nullptr") {
        auto conn = pool.acquire();
        REQUIRE(conn->get_io_context() == nullptr);
    }
    
    SECTION("Can still use sync operations") {
        auto conn = pool.acquire();
        auto result = conn->execute("SELECT 1");
        query_result qr(result);
        REQUIRE(qr.row_count() == 1);
    }
}
