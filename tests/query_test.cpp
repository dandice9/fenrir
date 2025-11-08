#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "../src/fenrir.hpp"

using namespace fenrir;
using Catch::Matchers::ContainsSubstring;

constexpr const char* TEST_CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

TEST_CASE("database_query - Basic SELECT", "[query]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_q (id SERIAL, name TEXT, val INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute("INSERT INTO test_q (name, val) VALUES ('Alice', 100), ('Bob', 200)");
    PQclear(insert_result);
    
    SECTION("Select all columns") {
        database_query query(conn);
        auto result = query.select("*").from("test_q").execute();
        
        REQUIRE(result.row_count() == 2);
        REQUIRE(result.column_count() == 3);  // id, name, val
    }
    
    SECTION("Select specific columns") {
        database_query query(conn);
        auto result = query.select("name, val").from("test_q").execute();
        
        REQUIRE(result.row_count() == 2);
        REQUIRE(result.column_count() == 2);
        
        auto name = result.get<std::string>(0, 0);
        REQUIRE(name.value() == "Alice");
        
        auto val = result.get<int>(0, 1);
        REQUIRE(val.value() == 100);
    }
    
    SECTION("Select with multiple columns specified separately") {
        database_query query(conn);
        auto result = query.select("name").select("val").from("test_q").execute();
        
        REQUIRE(result.row_count() == 2);
    }
}

TEST_CASE("database_query - WHERE Clauses", "[query][where]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_where (id SERIAL, name TEXT, age INT, city TEXT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO test_where (name, age, city) VALUES "
        "('Alice', 25, 'NYC'), "
        "('Bob', 30, 'LA'), "
        "('Charlie', 35, 'NYC'), "
        "('Dave', 28, 'Chicago')"
    );
    PQclear(insert_result);
    
    SECTION("Single WHERE condition") {
        database_query query(conn);
        auto result = query.select("*").from("test_where").where("age > 28").execute();
        
        REQUIRE(result.row_count() == 2);  // Bob and Charlie
    }
    
    SECTION("Multiple WHERE conditions") {
        database_query query(conn);
        auto result = query.select("*").from("test_where")
                          .where("city = 'NYC'")
                          .where("age > 25")
                          .execute();
        
        REQUIRE(result.row_count() == 1);  // Only Charlie
    }
    
    SECTION("WHERE with string values") {
        database_query query(conn);
        auto result = query.select("name, age").from("test_where")
                          .where("name = 'Alice'")
                          .execute();
        
        REQUIRE(result.row_count() == 1);
        auto name = result.get<std::string>(0, 0);
        REQUIRE(name.value() == "Alice");
    }
}

TEST_CASE("database_query - ORDER BY", "[query][orderby]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_order (id SERIAL, name TEXT, score INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO test_order (name, score) VALUES "
        "('Alice', 85), ('Bob', 92), ('Charlie', 78), ('Dave', 95)"
    );
    PQclear(insert_result);
    
    SECTION("ORDER BY ascending") {
        database_query query(conn);
        auto result = query.select("*").from("test_order")
                          .order_by("score", true)
                          .execute();
        
        REQUIRE(result.row_count() == 4);
        auto first_score = result.get<int>(0, 2);
        REQUIRE(first_score.value() == 78);  // Charlie
    }
    
    SECTION("ORDER BY descending") {
        database_query query(conn);
        auto result = query.select("*").from("test_order")
                          .order_by("score", false)
                          .execute();
        
        REQUIRE(result.row_count() == 4);
        auto first_score = result.get<int>(0, 2);
        REQUIRE(first_score.value() == 95);  // Dave
    }
    
    SECTION("ORDER BY multiple columns using raw SQL") {
        database_query query(conn);
        auto result = query.raw("SELECT * FROM test_order ORDER BY score DESC, name ASC");
        
        REQUIRE(result.row_count() == 4);
    }
}

TEST_CASE("database_query - LIMIT and OFFSET", "[query][limit]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_limit (id SERIAL, value INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO test_limit (value) VALUES (10), (20), (30), (40), (50), (60), (70), (80), (90), (100)"
    );
    PQclear(insert_result);
    
    SECTION("LIMIT only") {
        database_query query(conn);
        auto result = query.select("*").from("test_limit")
                          .limit(5)
                          .execute();
        
        REQUIRE(result.row_count() == 5);
    }
    
    SECTION("LIMIT with OFFSET") {
        database_query query(conn);
        auto result = query.select("*").from("test_limit")
                          .limit(3)
                          .offset(5)
                          .execute();
        
        REQUIRE(result.row_count() == 3);
        auto first_val = result.get<int>(0, 1);
        REQUIRE(first_val.value() == 60);
    }
    
    SECTION("OFFSET without LIMIT") {
        database_query query(conn);
        auto result = query.select("*").from("test_limit")
                          .offset(7)
                          .execute();
        
        REQUIRE(result.row_count() == 3);  // Last 3 records
    }
}

TEST_CASE("database_query - JOIN Operations", "[query][join]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_users = conn.execute("CREATE TEMP TABLE users (user_id SERIAL PRIMARY KEY, username TEXT)");
    PQclear(create_users);
    
    auto create_orders = conn.execute("CREATE TEMP TABLE orders (order_id SERIAL PRIMARY KEY, user_id INT, amount INT)");
    PQclear(create_orders);
    
    auto insert_users = conn.execute("INSERT INTO users (username) VALUES ('Alice'), ('Bob'), ('Charlie')");
    PQclear(insert_users);
    
    auto insert_orders = conn.execute(
        "INSERT INTO orders (user_id, amount) VALUES (1, 100), (1, 150), (2, 200), (3, 75)"
    );
    PQclear(insert_orders);
    
    SECTION("INNER JOIN using raw SQL") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT users.username, orders.amount FROM users "
            "INNER JOIN orders ON users.user_id = orders.user_id"
        );
        
        REQUIRE(result.row_count() == 4);
    }
    
    SECTION("LEFT JOIN using raw SQL") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT users.username, orders.amount FROM users "
            "LEFT JOIN orders ON users.user_id = orders.user_id"
        );
        
        REQUIRE(result.row_count() == 4);
    }
    
    SECTION("JOIN with WHERE clause") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT users.username, orders.amount FROM users "
            "INNER JOIN orders ON users.user_id = orders.user_id "
            "WHERE orders.amount > 100"
        );
        
        REQUIRE(result.row_count() == 2);
    }
}

TEST_CASE("database_query - GROUP BY and Aggregations", "[query][groupby]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE sales (id SERIAL, category TEXT, amount INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO sales (category, amount) VALUES "
        "('Electronics', 100), ('Electronics', 200), "
        "('Books', 50), ('Books', 75), ('Books', 25), "
        "('Clothing', 150)"
    );
    PQclear(insert_result);
    
    SECTION("GROUP BY with COUNT using raw SQL") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT category, COUNT(*) as count FROM sales GROUP BY category"
        );
        
        REQUIRE(result.row_count() == 3);
    }
    
    SECTION("GROUP BY with SUM using raw SQL") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT category, SUM(amount) as total FROM sales "
            "GROUP BY category ORDER BY total DESC"
        );
        
        REQUIRE(result.row_count() == 3);
        auto first_category = result.get<std::string>(0, 0);
        REQUIRE(first_category.value() == "Electronics");
    }
    
    SECTION("GROUP BY with HAVING using raw SQL") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT category, SUM(amount) as total FROM sales "
            "GROUP BY category HAVING SUM(amount) > 100"
        );
        
        // Electronics (300), Books (150), Clothing (150) - all > 100
        REQUIRE(result.row_count() == 3);
    }
}

TEST_CASE("database_query - Complex Queries", "[query][complex]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute(
        "CREATE TEMP TABLE products (id SERIAL, name TEXT, category TEXT, price INT, stock INT)"
    );
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO products (name, category, price, stock) VALUES "
        "('Laptop', 'Electronics', 1000, 5), "
        "('Mouse', 'Electronics', 25, 50), "
        "('Keyboard', 'Electronics', 75, 30), "
        "('Novel', 'Books', 15, 100), "
        "('Textbook', 'Books', 50, 20), "
        "('T-Shirt', 'Clothing', 20, 75)"
    );
    PQclear(insert_result);
    
    SECTION("Complex query with multiple clauses") {
        database_query query(conn);
        auto result = query.select("name, price, stock")
                          .from("products")
                          .where("category = 'Electronics'")
                          .where("price < 100")
                          .order_by("price", false)  // DESC
                          .limit(2)
                          .execute();
        
        REQUIRE(result.row_count() == 2);
        auto first_name = result.get<std::string>(0, 0);
        REQUIRE(first_name.value() == "Keyboard");
    }
    
    SECTION("Query with aggregate and filter using raw SQL") {
        database_query query(conn);
        auto result = query.raw(
            "SELECT category, AVG(price) as avg_price, SUM(stock) as total_stock "
            "FROM products GROUP BY category "
            "HAVING AVG(price) > 20 ORDER BY avg_price DESC"
        );
        
        REQUIRE(result.row_count() == 2);  // Electronics and Books
    }
}

TEST_CASE("database_query - DISTINCT", "[query][distinct]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_distinct (id SERIAL, category TEXT, tag TEXT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO test_distinct (category, tag) VALUES "
        "('A', 'tag1'), ('A', 'tag2'), ('B', 'tag1'), ('A', 'tag1'), ('C', 'tag3')"
    );
    PQclear(insert_result);
    
    SECTION("DISTINCT on single column") {
        database_query query(conn);
        auto result = query.select("DISTINCT category")
                          .from("test_distinct")
                          .execute();
        
        REQUIRE(result.row_count() == 3);  // A, B, C
    }
    
    SECTION("DISTINCT on multiple columns") {
        database_query query(conn);
        auto result = query.select("DISTINCT category, tag")
                          .from("test_distinct")
                          .execute();
        
        REQUIRE(result.row_count() == 4);  // (A,tag1), (A,tag2), (B,tag1), (C,tag3)
    }
}

TEST_CASE("database_query - Subqueries", "[query][subquery]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE employees (id SERIAL, name TEXT, salary INT, dept TEXT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO employees (name, salary, dept) VALUES "
        "('Alice', 60000, 'Engineering'), "
        "('Bob', 70000, 'Engineering'), "
        "('Charlie', 55000, 'Marketing'), "
        "('Dave', 80000, 'Engineering'), "
        "('Eve', 65000, 'Marketing')"
    );
    PQclear(insert_result);
    
    SECTION("Subquery in WHERE clause") {
        // Find employees earning more than average
        database_query query(conn);
        auto result = query.select("name, salary")
                          .from("employees")
                          .where("salary > (SELECT AVG(salary) FROM employees)")
                          .execute();
        
        REQUIRE(result.row_count() >= 2);
    }
    
    SECTION("Subquery with IN clause") {
        database_query query(conn);
        auto result = query.select("name, salary")
                          .from("employees")
                          .where("dept IN (SELECT DISTINCT dept FROM employees WHERE dept = 'Engineering')")
                          .execute();
        
        REQUIRE(result.row_count() == 3);
    }
}

TEST_CASE("database_query - NULL Handling", "[query][null]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_null (id SERIAL, name TEXT, optional_value INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO test_null (name, optional_value) VALUES "
        "('Alice', 100), ('Bob', NULL), ('Charlie', 200), ('Dave', NULL)"
    );
    PQclear(insert_result);
    
    SECTION("IS NULL condition") {
        database_query query(conn);
        auto result = query.select("*")
                          .from("test_null")
                          .where("optional_value IS NULL")
                          .execute();
        
        REQUIRE(result.row_count() == 2);  // Bob and Dave
    }
    
    SECTION("IS NOT NULL condition") {
        database_query query(conn);
        auto result = query.select("*")
                          .from("test_null")
                          .where("optional_value IS NOT NULL")
                          .execute();
        
        REQUIRE(result.row_count() == 2);  // Alice and Charlie
    }
    
    SECTION("Retrieve NULL values") {
        database_query query(conn);
        auto result = query.select("*")
                          .from("test_null")
                          .where("name = 'Bob'")
                          .execute();
        
        REQUIRE(result.row_count() == 1);
        auto val = result.get<int>(0, 2);
        REQUIRE_FALSE(val.has_value());  // Should be NULL/empty optional
    }
}

TEST_CASE("database_query - Query Building", "[query][builder]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_builder (id SERIAL, val INT)");
    PQclear(create_result);
    
    auto insert_result = conn.execute("INSERT INTO test_builder (val) VALUES (1), (2), (3), (4), (5)");
    PQclear(insert_result);
    
    SECTION("Build query incrementally") {
        database_query query(conn);
        query.select("*").from("test_builder");
        
        // Add condition dynamically
        bool apply_filter = true;
        if (apply_filter) {
            query.where("val > 2");
        }
        
        auto result = query.execute();
        REQUIRE(result.row_count() == 3);
    }
    
    SECTION("Reuse query builder") {
        database_query query(conn);
        query.select("*").from("test_builder");
        
        auto result1 = query.execute();
        REQUIRE(result1.row_count() == 5);
        
        // Can execute same query multiple times
        auto result2 = query.execute();
        REQUIRE(result2.row_count() == 5);
    }
}

TEST_CASE("database_query - Result Retrieval", "[query][result]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    auto create_result = conn.execute("CREATE TEMP TABLE test_result (id SERIAL, name TEXT, age INT, score REAL)");
    PQclear(create_result);
    
    auto insert_result = conn.execute(
        "INSERT INTO test_result (name, age, score) VALUES "
        "('Alice', 25, 95.5), ('Bob', 30, 87.3), ('Charlie', 28, 92.1)"
    );
    PQclear(insert_result);
    
    SECTION("Get different data types") {
        database_query query(conn);
        auto result = query.select("*").from("test_result").where("name = 'Alice'").execute();
        
        REQUIRE(result.row_count() == 1);
        
        auto name = result.get<std::string>(0, 1);
        REQUIRE(name.has_value());
        REQUIRE(name.value() == "Alice");
        
        auto age = result.get<int>(0, 2);
        REQUIRE(age.has_value());
        REQUIRE(age.value() == 25);
        
        auto score = result.get<double>(0, 3);
        REQUIRE(score.has_value());
        REQUIRE(score.value() > 95.0);
    }
    
    SECTION("Access multiple rows") {
        database_query query(conn);
        auto result = query.select("name, age")
                          .from("test_result")
                          .order_by("age", true)  // ASC
                          .execute();
        
        REQUIRE(result.row_count() == 3);
        
        auto first = result.get<std::string>(0, 0);
        REQUIRE(first.value() == "Alice");
        
        auto second = result.get<std::string>(1, 0);
        REQUIRE(second.value() == "Charlie");
        
        auto third = result.get<std::string>(2, 0);
        REQUIRE(third.value() == "Bob");
    }
}

TEST_CASE("database_query - Error Handling", "[query][error]") {
    database_connection conn(TEST_CONNECTION_STRING);
    
    SECTION("Query non-existent table") {
        database_query query(conn);
        REQUIRE_THROWS_AS(
            query.select("*").from("non_existent_table").execute(),
            database_error
        );
    }
    
    SECTION("Invalid column name") {
        auto create_result = conn.execute("CREATE TEMP TABLE test_error (id SERIAL, name TEXT)");
        PQclear(create_result);
        
        database_query query(conn);
        REQUIRE_THROWS_AS(
            query.select("invalid_column").from("test_error").execute(),
            database_error
        );
    }
    
    SECTION("Syntax error in WHERE clause") {
        auto create_result = conn.execute("CREATE TEMP TABLE test_syntax (id SERIAL, val INT)");
        PQclear(create_result);
        
        database_query query(conn);
        REQUIRE_THROWS_AS(
            query.select("*").from("test_syntax").where("val = = 5").execute(),
            database_error
        );
    }
}
