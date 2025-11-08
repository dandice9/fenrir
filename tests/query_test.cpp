#include <catch2/catch_test_macros.hpp>
#include "../src/fenrir.hpp"

using namespace fenrir;

constexpr const char* TEST_CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

TEST_CASE("database_query - Basic SELECT", "[query]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_q (id SERIAL, name TEXT, val INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute("INSERT INTO test_q (name, val) VALUES ('Alice', 100), ('Bob', 200)");
    PQclear(insert_result);
    
    database_query query(conn);
    auto result = query.select("*").from("test_q").execute();
    
    REQUIRE(result.row_count() == 2);
}
