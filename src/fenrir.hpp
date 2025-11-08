#pragma once

/**
 * Fenrir - Modern C++20 PostgreSQL libpq Wrapper
 * 
 * A header-only library providing:
 * - RAII resource management
 * - Type-safe query execution
 * - Transaction support with savepoints
 * - Thread-safe connection pooling
 * - Stored procedure wrappers
 * - C++20 features: concepts, std::expected, std::optional, std::format
 * 
 * Usage:
 *   #include <fenrir/fenrir.hpp>
 *   using namespace fenrir;
 * 
 * Examples:
 *   // Simple connection and query
 *   database_connection conn("host=localhost dbname=mydb user=user password=pass");
 *   database_query query(conn);
 *   auto result = query.raw("SELECT * FROM users");
 * 
 *   // Connection pool
 *   database_pool::pool_config config{
 *       .connection_string = "...",
 *       .min_connections = 5,
 *       .max_connections = 20
 *   };
 *   database_pool pool(config);
 *   auto conn = pool.acquire();
 * 
 *   // Transactions
 *   with_transaction(conn, [](database_transaction& txn) {
 *       txn.execute("INSERT INTO logs (msg) VALUES ('test')");
 *   });
 * 
 * See README.md for full documentation.
 */

// Compatibility layer for std::expected

// Core components
#include "database_connection.hpp"
#include "database_query.hpp"
#include "database_transaction.hpp"
#include "database_pool.hpp"
#include "database_stored_procedure.hpp"

// Version information
#define FENRIR_VERSION_MAJOR 1
#define FENRIR_VERSION_MINOR 0
#define FENRIR_VERSION_PATCH 0

namespace fenrir {

    /**
     * Library version information
     */
    constexpr struct version_info {
        int major = FENRIR_VERSION_MAJOR;
        int minor = FENRIR_VERSION_MINOR;
        int patch = FENRIR_VERSION_PATCH;
        
        [[nodiscard]] constexpr const char* string() const noexcept {
            return "1.0.0";
        }
    } version;

} // namespace fenrir