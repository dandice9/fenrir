#include <catch2/catch_test_macros.hpp>
#include "../src/fenrir.hpp"

using namespace fenrir;

constexpr const char* TEST_CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

TEST_CASE("database_pool - Acquire", "[pool]") {
    database_pool::pool_config config{
        .connection_string = TEST_CONNECTION_STRING,
        .min_connections = 2,
        .max_connections = 5
    };
    
    database_pool pool(config);
    auto conn = pool.acquire();
    
    REQUIRE(conn->is_connected());
}
