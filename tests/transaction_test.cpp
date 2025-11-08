#include <catch2/catch_test_macros.hpp>
#include "../src/fenrir.hpp"

using namespace fenrir;

constexpr const char* TEST_CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

TEST_CASE("database_transaction - Commit", "[transaction]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_txn (id SERIAL, val INT)");
    PQclear(create_result);
    
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
