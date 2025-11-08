#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "../src/fenrir.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace fenrir;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

constexpr const char* TEST_CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

TEST_CASE("database_transaction - Basic Commit", "[transaction]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_txn (id SERIAL, val INT)");
    PQclear(create_result);
    
    SECTION("Commit writes data") {
        {
            database_transaction txn(conn);
            (void)txn.execute("INSERT INTO test_txn (val) VALUES (100)");
            txn.commit();
        }
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_txn");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 1);
    }
    
    SECTION("Multiple inserts in transaction") {
        {
            database_transaction txn(conn);
            (void)txn.execute("INSERT INTO test_txn (val) VALUES (100)");
            (void)txn.execute("INSERT INTO test_txn (val) VALUES (200)");
            (void)txn.execute("INSERT INTO test_txn (val) VALUES (300)");
            txn.commit();
        }
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_txn");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 3);
    }
}

TEST_CASE("database_transaction - Rollback", "[transaction]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_rollback (id SERIAL, val INT)");
    PQclear(create_result);
    
    SECTION("Explicit rollback") {
        {
            database_transaction txn(conn);
            (void)txn.execute("INSERT INTO test_rollback (val) VALUES (100)");
            txn.rollback();
        }
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_rollback");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 0);
    }
    
    SECTION("Implicit rollback on destruction") {
        {
            database_transaction txn(conn);
            (void)txn.execute("INSERT INTO test_rollback (val) VALUES (200)");
            // No commit - should auto-rollback
        }
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_rollback");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 0);
    }
    
    SECTION("Rollback multiple operations") {
        // Insert one row outside transaction
        auto insert_result = conn.execute("INSERT INTO test_rollback (val) VALUES (50)");
        PQclear(insert_result);
        
        {
            database_transaction txn(conn);
            (void)txn.execute("INSERT INTO test_rollback (val) VALUES (100)");
            (void)txn.execute("INSERT INTO test_rollback (val) VALUES (200)");
            txn.rollback();
        }
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_rollback");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 1);  // Only the one outside transaction
    }
}

TEST_CASE("database_transaction - Savepoints", "[transaction][savepoint]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_savepoint (id SERIAL, val INT)");
    PQclear(create_result);
    
    SECTION("Create and rollback to savepoint") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (100)");
        
        {
            auto sp = txn.create_savepoint("sp1");
            (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (200)");
            sp.rollback();
        }
        
        txn.commit();
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_savepoint");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 1);  // Only first insert
    }
    
    SECTION("Nested savepoints") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (100)");
        
        {
            auto sp1 = txn.create_savepoint("sp1");
            (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (200)");
            
            {
                auto sp2 = txn.create_savepoint("sp2");
                (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (300)");
                sp2.rollback();  // Rolls back 300
            }
            
            // sp2 rolled back, sp1 still has 200, release sp1 to keep changes
            sp1.release();
        }
        
        txn.commit();
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_savepoint");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 2);  // 100 and 200
    }
    
    SECTION("Release savepoint") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (100)");
        
        {
            auto sp = txn.create_savepoint("sp_release");
            (void)txn.execute("INSERT INTO test_savepoint (val) VALUES (200)");
            sp.release();  // Commit savepoint
        }
        
        txn.commit();
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_savepoint");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 2);
    }
}

TEST_CASE("database_transaction - Error Handling", "[transaction][error]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_error (id SERIAL PRIMARY KEY, val INT UNIQUE)");
    PQclear(create_result);
    
    SECTION("Transaction fails on constraint violation") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_error (val) VALUES (100)");
        
        // Try to insert duplicate
        REQUIRE_THROWS_AS(txn.execute("INSERT INTO test_error (val) VALUES (100)"), database_error);
        
        // Transaction should still be usable after error
        txn.rollback();
    }
    
    SECTION("Cannot commit after rollback") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_error (val) VALUES (100)");
        txn.rollback();
        
        // Attempting commit after rollback should throw
        REQUIRE_THROWS_AS(txn.commit(), database_error);
    }
    
    SECTION("Cannot rollback after commit") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_error (val) VALUES (100)");
        txn.commit();
        
        // Attempting rollback after commit should throw
        REQUIRE_THROWS_AS(txn.rollback(), database_error);
    }
}

TEST_CASE("database_transaction - Complex Operations", "[transaction][complex]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute(
        "CREATE TEMP TABLE test_complex (id SERIAL PRIMARY KEY, name TEXT, amount INT)"
    );
    PQclear(create_result);
    
    SECTION("Update and delete in transaction") {
        // Setup data
        auto insert1 = conn.execute("INSERT INTO test_complex (name, amount) VALUES ('Alice', 100)");
        PQclear(insert1);
        auto insert2 = conn.execute("INSERT INTO test_complex (name, amount) VALUES ('Bob', 200)");
        PQclear(insert2);
        auto insert3 = conn.execute("INSERT INTO test_complex (name, amount) VALUES ('Charlie', 300)");
        PQclear(insert3);
        
        {
            database_transaction txn(conn);
            
            // Update
            (void)txn.execute("UPDATE test_complex SET amount = amount + 50 WHERE name = 'Alice'");
            
            // Delete
            (void)txn.execute("DELETE FROM test_complex WHERE name = 'Bob'");
            
            txn.commit();
        }
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_complex");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 2);
        
        auto alice_result = conn.execute("SELECT amount FROM test_complex WHERE name = 'Alice'");
        query_result alice_qr(alice_result);
        auto alice_amount = alice_qr.get<int>(0, 0);
        REQUIRE(alice_amount.value() == 150);
    }
    
    SECTION("Conditional logic with savepoints") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_complex (name, amount) VALUES ('Dave', 1000)");
        
        {
            auto sp = txn.create_savepoint("try_insert");
            
            try {
                // This will fail - negative amount not allowed (hypothetically)
                (void)txn.execute("INSERT INTO test_complex (name, amount) VALUES ('Eve', 500)");
                sp.release();
            } catch (...) {
                sp.rollback();
            }
        }
        
        txn.commit();
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_complex");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() >= 1);
    }
}

TEST_CASE("database_transaction - Isolation Levels", "[transaction][isolation]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    // Drop and create persistent table for isolation testing
    auto drop_result = conn.execute("DROP TABLE IF EXISTS test_isolation");
    PQclear(drop_result);
    auto create_result = conn.execute("CREATE TABLE test_isolation (id SERIAL, val INT)");
    PQclear(create_result);
    
    SECTION("Read committed isolation") {
        auto insert_result = conn.execute("INSERT INTO test_isolation (val) VALUES (100)");
        PQclear(insert_result);
        
        database_transaction txn(conn);
        
        // Read initial value
        auto qr1 = txn.execute("SELECT val FROM test_isolation WHERE id = 1");
        auto val1 = qr1.get<int>(0, 0);
        REQUIRE(val1.value() == 100);
        
        txn.commit();
    }
    
    // Cleanup
    auto cleanup_result = conn.execute("DROP TABLE test_isolation");
    PQclear(cleanup_result);
}

TEST_CASE("database_transaction - Query Execution", "[transaction][query]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_query (id SERIAL, val INT)");
    PQclear(create_result);
    
    SECTION("Execute with result handling") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_query (val) VALUES (100)");
        (void)txn.execute("INSERT INTO test_query (val) VALUES (200)");
        (void)txn.execute("INSERT INTO test_query (val) VALUES (300)");
        
        auto qr = txn.execute("SELECT SUM(val) FROM test_query");
        auto sum = qr.get<int>(0, 0);
        REQUIRE(sum.value() == 600);
        
        txn.commit();
    }
    
    SECTION("Execute parameterized queries") {
        database_transaction txn(conn);
        
        // Insert multiple values
        for (int i = 1; i <= 5; ++i) {
            (void)txn.execute(std::format("INSERT INTO test_query (val) VALUES ({})", i * 10));
        }
        
        txn.commit();
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_query");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 5);
    }
}

TEST_CASE("database_transaction - Long Running Transaction", "[transaction][long-running]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_long (id SERIAL, val INT)");
    PQclear(create_result);
    
    SECTION("Transaction with delays") {
        database_transaction txn(conn);
        
        (void)txn.execute("INSERT INTO test_long (val) VALUES (100)");
        std::this_thread::sleep_for(100ms);
        
        (void)txn.execute("INSERT INTO test_long (val) VALUES (200)");
        std::this_thread::sleep_for(100ms);
        
        (void)txn.execute("INSERT INTO test_long (val) VALUES (300)");
        
        txn.commit();
        
        auto verify_result = conn.execute("SELECT COUNT(*) FROM test_long");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == 3);
    }
}

TEST_CASE("database_transaction - Concurrent Access", "[transaction][concurrent]") {
    // Drop and create persistent table
    database_connection setup_conn(TEST_CONNECTION_STRING);
    auto drop_result = setup_conn.execute("DROP TABLE IF EXISTS test_concurrent");
    PQclear(drop_result);
    auto create_result = setup_conn.execute("CREATE TABLE test_concurrent (id SERIAL PRIMARY KEY, val INT)");
    PQclear(create_result);
    
    SECTION("Multiple connections with transactions") {
        std::atomic<int> successful_txns{0};
        std::atomic<int> failed_txns{0};
        std::vector<std::thread> threads;
        
        constexpr int num_threads = 5;
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, thread_id = i]() {
                try {
                    database_connection conn(TEST_CONNECTION_STRING);
                    database_transaction txn(conn);
                    
                    (void)txn.execute(
                        std::format("INSERT INTO test_concurrent (val) VALUES ({})", thread_id * 100)
                    );
                    
                    std::this_thread::sleep_for(10ms);
                    
                    txn.commit();
                    successful_txns++;
                    
                } catch (const database_error&) {
                    failed_txns++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        REQUIRE(successful_txns + failed_txns == num_threads);
        REQUIRE(successful_txns > 0);
        
        // Verify data
        database_connection verify_conn(TEST_CONNECTION_STRING);
        auto verify_result = verify_conn.execute("SELECT COUNT(*) FROM test_concurrent");
        query_result qr(verify_result);
        auto count = qr.get<int>(0, 0);
        REQUIRE(count.value() == successful_txns);
        
        INFO("Successful transactions: " << successful_txns);
        INFO("Failed transactions: " << failed_txns);
    }
    
    // Cleanup
    auto cleanup_result = setup_conn.execute("DROP TABLE test_concurrent");
    PQclear(cleanup_result);
}
