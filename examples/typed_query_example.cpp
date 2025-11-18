/**
 * Typed Query Builder Example
 * 
 * Demonstrates compile-time safety features of the typed_query_builder
 */

#include "../src/fenrir.hpp"
#include <iostream>

using namespace fenrir;

int main() {
    try {
        // Connect to database
        database_connection conn("host=localhost dbname=testdb user=testuser password=testpass");
        
        std::cout << "=== Type-Safe Query Builder Examples ===\n\n";
        
        // ========================================================================
        // Example 1: SELECT Query (compile-time validated)
        // ========================================================================
        std::cout << "1. SELECT with compile-time validation:\n";
        {
            typed_query_builder builder(conn);
            
            // This compiles: correct order
            auto result = builder.select("id, name, email")
                               .from("users")
                               .where("age > 18")
                               .order_by("name")
                               .limit(10)
                               .execute();
            
            std::cout << "   Query: " << builder.select("*").from("users").get_query() << "\n";
            std::cout << "   ✅ Compiled successfully!\n\n";
            
            // ❌ This won't compile: from() called twice
            // auto bad1 = builder.select("*").from("users").from("orders");
            
            // ❌ This won't compile: order_by() before from()
            // auto bad2 = builder.select("*").order_by("id").from("users");
            
            // ❌ This won't compile: execute() before from()
            // auto bad3 = builder.select("*").execute();
        }
        
        // ========================================================================
        // Example 2: INSERT Query (requires VALUES)
        // ========================================================================
        std::cout << "2. INSERT with compile-time validation:\n";
        {
            typed_query_builder builder(conn);
            
            // This compiles: correct order
            auto query = builder.insert_into("users", "name, email")
                              .values("'John Doe', 'john@example.com'")
                              .returning("id");
            
            std::cout << "   Query: " << query.get_query() << "\n";
            std::cout << "   ✅ Compiled successfully!\n\n";
            
            // ❌ This won't compile: execute() before values()
            // auto bad = builder.insert_into("users", "name").execute();
            
            // ❌ This won't compile: where() on INSERT (not allowed yet in this version)
            // Note: PostgreSQL does support WHERE in INSERT...SELECT, future enhancement
        }
        
        // ========================================================================
        // Example 3: UPDATE Query (requires SET)
        // ========================================================================
        std::cout << "3. UPDATE with compile-time validation:\n";
        {
            typed_query_builder builder(conn);
            
            // This compiles: correct order
            auto query = builder.update("users")
                              .set("name = 'Jane Doe'")
                              .where("id = 1")
                              .returning("*");
            
            std::cout << "   Query: " << query.get_query() << "\n";
            std::cout << "   ✅ Compiled successfully!\n\n";
            
            // ❌ This won't compile: execute() before set()
            // auto bad1 = builder.update("users").execute();
            
            // ❌ This won't compile: where() before set()
            // auto bad2 = builder.update("users").where("id = 1").set("name = 'X'");
            
            // ❌ This won't compile: set() called twice
            // auto bad3 = builder.update("users").set("a = 1").set("b = 2");
        }
        
        // ========================================================================
        // Example 4: DELETE Query
        // ========================================================================
        std::cout << "4. DELETE with compile-time validation:\n";
        {
            typed_query_builder builder(conn);
            
            // This compiles: correct order
            auto query = builder.delete_from("users")
                              .where("created_at < NOW() - INTERVAL '1 year'")
                              .returning("id");
            
            std::cout << "   Query: " << query.get_query() << "\n";
            std::cout << "   ✅ Compiled successfully!\n\n";
            
            // ❌ This won't compile: from() called on DELETE (table already specified)
            // auto bad = builder.delete_from("users").from("orders");
        }
        
        // ========================================================================
        // Example 5: Complex SELECT with JOINs
        // ========================================================================
        std::cout << "5. Complex SELECT with JOINs:\n";
        {
            typed_query_builder builder(conn);
            
            auto query = builder.select("u.name, COUNT(o.id) as order_count")
                              .from("users u")
                              .left_join("orders o", "o.user_id = u.id")
                              .where("u.active = true")
                              .group_by("u.id, u.name")
                              .having("COUNT(o.id) > 5")
                              .order_by("order_count", false)
                              .limit(20);
            
            std::cout << "   Query: " << query.get_query() << "\n";
            std::cout << "   ✅ Compiled successfully!\n\n";
        }
        
        // ========================================================================
        // Example 6: Type information at compile time
        // ========================================================================
        std::cout << "6. Query type information:\n";
        {
            typed_query_builder builder(conn);
            
            auto select_query = builder.select("*").from("users");
            auto insert_query = builder.insert_into("users", "name").values("'Test'");
            auto update_query = builder.update("users").set("name = 'X'");
            auto delete_query = builder.delete_from("users");
            
            std::cout << "   SELECT query type: " << select_query.query_type_name() << "\n";
            std::cout << "   INSERT query type: " << insert_query.query_type_name() << "\n";
            std::cout << "   UPDATE query type: " << update_query.query_type_name() << "\n";
            std::cout << "   DELETE query type: " << delete_query.query_type_name() << "\n";
            std::cout << "   ✅ All types detected at compile time!\n\n";
        }
        
        // ========================================================================
        // Example 7: Multiple WHERE conditions
        // ========================================================================
        std::cout << "7. Multiple WHERE conditions (chained as AND):\n";
        {
            typed_query_builder builder(conn);
            
            auto query = builder.select("*")
                              .from("products")
                              .where("price > 100")
                              .where("in_stock = true")
                              .where("category = 'electronics'");
            
            std::cout << "   Query: " << query.get_query() << "\n";
            std::cout << "   ✅ Multiple WHERE clauses become AND conditions!\n\n";
        }
        
        // ========================================================================
        // Example 8: Legacy builder (no compile-time checks)
        // ========================================================================
        std::cout << "8. Legacy database_query (backward compatible):\n";
        {
            database_query legacy(conn);
            
            // No compile-time checks, runtime flexibility
            auto result = legacy.select("*")
                               .from("users")
                               .where("active = true")
                               .order_by("created_at", false)
                               .limit(5)
                               .execute();
            
            std::cout << "   Query: " << legacy.get_query() << "\n";
            std::cout << "   ✅ Works with runtime validation!\n\n";
        }
        
        std::cout << "=== All Examples Completed Successfully! ===\n";
        std::cout << "\nKey Benefits:\n";
        std::cout << "  ✅ Compile-time error prevention\n";
        std::cout << "  ✅ Correct query construction order enforced\n";
        std::cout << "  ✅ Type-safe query building with C++20 concepts\n";
        std::cout << "  ✅ Zero runtime overhead (all checks at compile time)\n";
        std::cout << "  ✅ Better IDE autocomplete and error messages\n";
        std::cout << "  ✅ Backward compatible with legacy builder\n";
        
    } catch (const database_error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
