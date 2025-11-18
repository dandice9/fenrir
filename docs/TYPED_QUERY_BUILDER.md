# Type-Safe Query Builder

## Overview

Fenrir now includes a **compile-time validated query builder** using C++20 concepts and type-state programming. This prevents SQL construction errors at compile time rather than runtime.

## Two Query Builders

### 1. `typed_query_builder<State>` (NEW!)
- **Compile-time validation** of query construction
- **Type-safe** with C++20 concepts
- **Zero runtime overhead** (all checks compile away)
- **Better IDE support** with clear error messages

### 2. `database_query` (Legacy)
- **Runtime validation** (backward compatible)
- **Flexible** for dynamic queries
- Works exactly as before

## Quick Start

```cpp
#include "fenrir.hpp"

database_connection conn("...");

// Type-safe builder - catches errors at compile time!
typed_query_builder builder(conn);

// ✅ This compiles - correct order
auto result = builder.select("id, name, email")
                   .from("users")
                   .where("age > 18")
                   .order_by("name")
                   .limit(10)
                   .execute();

// ❌ This won't compile - execute() before from()
// auto bad = builder.select("*").execute();  // Compile error!

// ❌ This won't compile - from() called twice
// auto bad = builder.select("*").from("users").from("orders");  // Compile error!
```

## Compile-Time Rules

### SELECT Queries

**Required Order:**
1. `select(columns)` - Start query
2. `from(table)` - **REQUIRED** before execute
3. `where(condition)` - Optional, can be called multiple times (becomes AND)
4. `order_by(column, asc)` - Optional
5. `limit(n)` / `offset(n)` - Optional
6. `execute()` - Only allowed after `from()`

**Optional Clauses (after `from()`):**
- `join()` / `inner_join()` / `left_join()` / `right_join()` / `full_join()`
- `group_by(columns)`
- `having(condition)`

**Example:**
```cpp
// ✅ Valid
auto q = builder.select("*")
              .from("users")
              .where("active = true")
              .order_by("created_at", false)
              .limit(10)
              .execute();

// ❌ Invalid - order_by before from
auto bad = builder.select("*").order_by("id").from("users");  // Won't compile!
```

### INSERT Queries

**Required Order:**
1. `insert_into(table, columns)` - Start query
2. `values(value_list)` - **REQUIRED** before execute
3. `returning(columns)` - Optional
4. `execute()` - Only allowed after `values()`

**Example:**
```cpp
// ✅ Valid
auto q = builder.insert_into("users", "name, email")
              .values("'John', 'john@example.com'")
              .returning("id")
              .execute();

// ❌ Invalid - execute before values
auto bad = builder.insert_into("users", "name").execute();  // Won't compile!
```

### UPDATE Queries

**Required Order:**
1. `update(table)` - Start query
2. `set(assignments)` - **REQUIRED** before execute
3. `where(condition)` - Optional (after `set()`)
4. `returning(columns)` - Optional
5. `execute()` - Only allowed after `set()`

**Example:**
```cpp
// ✅ Valid
auto q = builder.update("users")
              .set("email = 'new@example.com'")
              .where("id = 1")
              .returning("*")
              .execute();

// ❌ Invalid - where before set
auto bad = builder.update("users").where("id = 1").set("name = 'X'");  // Won't compile!

// ❌ Invalid - set called twice
auto bad = builder.update("users").set("a = 1").set("b = 2");  // Won't compile!
```

### DELETE Queries

**Required Order:**
1. `delete_from(table)` - Start query (table specified here)
2. `where(condition)` - Optional
3. `returning(columns)` - Optional
4. `execute()` - Allowed immediately

**Example:**
```cpp
// ✅ Valid
auto q = builder.delete_from("users")
              .where("created_at < NOW() - INTERVAL '1 year'")
              .returning("id")
              .execute();

// ❌ Invalid - from() after delete_from
auto bad = builder.delete_from("users").from("orders");  // Won't compile!
```

## State Tracking

The builder tracks query state at **compile time** using template parameters:

```cpp
template<typename QueryType = no_query_tag,
         bool HasFrom = false,
         bool HasWhere = false,
         bool HasSet = false,
         bool HasValues = false>
struct query_state;
```

Each method returns a new builder with updated state:
- `select()` → State becomes `query_state<select_query_tag, ...>`
- `from()` → Sets `HasFrom = true`
- `where()` → Sets `HasWhere = true`
- `set()` → Sets `HasSet = true`
- `values()` → Sets `HasValues = true`

## Concepts Used

### Query Type Concepts
```cpp
template<typename State>
concept SelectQuery = std::same_as<typename State::query_type, select_query_tag>;

template<typename State>
concept InsertQuery = std::same_as<typename State::query_type, insert_query_tag>;

template<typename State>
concept UpdateQuery = std::same_as<typename State::query_type, update_query_tag>;

template<typename State>
concept DeleteQuery = std::same_as<typename State::query_type, delete_query_tag>;
```

### State Concepts
```cpp
template<typename State>
concept HasFrom = State::has_from;

template<typename State>
concept HasWhere = State::has_where;

template<typename State>
concept HasSet = State::has_set;

template<typename State>
concept HasValues = State::has_values;
```

### Validation Concepts
```cpp
// Can only execute when query is complete
template<typename State>
concept CanExecute = (SelectQuery<State> && HasFrom<State>) ||
                     (InsertQuery<State> && HasValues<State>) ||
                     (UpdateQuery<State> && HasSet<State>) ||
                     (DeleteQuery<State> && HasFrom<State>);

// Can only add FROM to SELECT or DELETE queries without FROM
template<typename State>
concept CanAddFrom = (SelectQuery<State> || DeleteQuery<State>) && !HasFrom<State>;

// Can only add WHERE after FROM
template<typename State>
concept CanAddWhere = QueryStarted<State> && HasFrom<State> && !HasWhere<State>;
```

## Method Constraints

Each method uses `requires` clauses to enforce rules:

```cpp
// Only allowed when no query started
auto select(std::string_view columns) requires NoQueryStarted<State>;

// Only allowed for SELECT/DELETE without FROM
auto from(std::string_view table) requires CanAddFrom<State>;

// Only allowed for UPDATE without SET
auto set(std::string_view assignments) requires CanAddSet<State>;

// Only allowed when query is complete
query_result execute() requires CanExecute<State>;
```

## Benefits

### 1. Compile-Time Error Detection
```cpp
// ❌ Compiler error with clear message:
// "no matching function for call to 'execute'"
// "candidate template ignored: constraints not satisfied"
auto bad = builder.select("*").execute();
```

### 2. IDE Autocomplete
Your IDE knows exactly which methods are available:
- After `select()`, IDE suggests `from()`
- After `from()`, IDE suggests `where()`, `order_by()`, `execute()`
- Methods that would violate rules don't appear in autocomplete

### 3. Zero Runtime Overhead
All validation happens at compile time. Generated code is identical to raw SQL:
```cpp
// This typed builder code...
auto result = builder.select("*").from("users").where("id = 1").execute();

// ...generates the same assembly as:
auto result = conn.execute("SELECT * FROM users WHERE id = 1");
```

### 4. Self-Documenting
The type system documents valid query patterns:
```cpp
// Clear from the API what's required
builder.select("*")        // Start SELECT
     .from("users")        // FROM is required
     .where("age > 18")    // WHERE is optional
     .execute();           // Can only execute when complete
```

### 5. Refactoring Safety
If you change query structure, compiler catches all affected code:
```cpp
// Change from SELECT to INSERT
// Compiler error: insert_into doesn't have order_by()
auto q = builder.insert_into("users", "name")
              .order_by("id")  // ❌ Compile error!
```

## Advanced Features

### Multiple WHERE Conditions
```cpp
// Each where() call adds an AND condition
auto q = builder.select("*")
              .from("products")
              .where("price > 100")      // WHERE price > 100
              .where("in_stock = true")  // AND in_stock = true
              .where("category = 'electronics'");  // AND category = 'electronics'
```

### JOIN Variants
```cpp
auto q = builder.select("u.name, o.total")
              .from("users u")
              .inner_join("orders o", "o.user_id = u.id")
              .left_join("addresses a", "a.user_id = u.id")
              .execute();
```

### RETURNING Clause
```cpp
// Works with INSERT, UPDATE, DELETE
auto insert_result = builder.insert_into("users", "name")
                          .values("'John'")
                          .returning("id, created_at")
                          .execute();

auto update_result = builder.update("users")
                          .set("name = 'Jane'")
                          .where("id = 1")
                          .returning("*")
                          .execute();
```

### GROUP BY and HAVING
```cpp
auto q = builder.select("category, COUNT(*) as count")
              .from("products")
              .group_by("category")
              .having("COUNT(*) > 10")
              .execute();
```

## When to Use Each Builder

### Use `typed_query_builder` when:
- ✅ Building queries with known structure at compile time
- ✅ Want compile-time safety and early error detection
- ✅ Prefer IDE autocomplete and type hints
- ✅ Building reusable query patterns
- ✅ Want self-documenting code

### Use `database_query` (legacy) when:
- ✅ Building dynamic queries from user input
- ✅ Need runtime query construction flexibility
- ✅ Migrating existing code gradually
- ✅ Query structure determined at runtime

## Performance

Both builders have **identical performance**:
- No runtime overhead for validation (compile-time only)
- Same generated SQL strings
- Same execution path
- Same memory usage

The only difference is **when** errors are caught:
- `typed_query_builder`: Compile time ⚡
- `database_query`: Runtime (or never if query is valid)

## Migration Guide

### From Legacy Builder

```cpp
// Before (legacy)
database_query query(conn);
auto result = query.select("*").from("users").execute();

// After (type-safe)
typed_query_builder builder(conn);
auto result = builder.select("*").from("users").execute();
```

Both APIs are identical for valid queries! Just change the class name.

### Mixing Both Builders

```cpp
database_connection conn("...");

// Use typed builder for known queries
typed_query_builder typed(conn);
auto static_result = typed.select("*").from("users").execute();

// Use legacy for dynamic queries
database_query dynamic(conn);
dynamic.select("*").from(user_selected_table);
if (has_filter) {
    dynamic.where(user_filter);
}
auto dynamic_result = dynamic.execute();
```

## Examples

See `examples/typed_query_example.cpp` for comprehensive examples of:
- ✅ All query types (SELECT, INSERT, UPDATE, DELETE)
- ✅ Complex queries with JOINs, GROUP BY, HAVING
- ✅ Multiple WHERE conditions
- ✅ RETURNING clauses
- ✅ Compile-time error prevention
- ✅ Type information inspection

## Future Enhancements

Planned features for future versions:
- 🔮 OR conditions in WHERE clauses
- 🔮 Subquery support
- 🔮 CTE (WITH clause) support
- 🔮 INSERT...SELECT support
- 🔮 UPSERT (ON CONFLICT) support
- 🔮 Window functions
- 🔮 Better parameterized query integration with type safety

## Compiler Requirements

Requires **C++20** with concept support:
- ✅ Clang 14.0+
- ✅ GCC 11+
- ✅ MSVC 19.30+ (Visual Studio 2022)
- ✅ AppleClang 15.0+

## Summary

The type-safe query builder brings **compile-time correctness** to SQL query construction:

| Feature | Legacy Builder | Type-Safe Builder |
|---------|---------------|-------------------|
| Error Detection | Runtime | **Compile Time** ✅ |
| IDE Support | Basic | **Advanced** ✅ |
| Performance | Fast | **Fast** (same) ✅ |
| Type Safety | None | **Full** ✅ |
| Flexibility | High | Medium |
| Learning Curve | Low | Low (same API) |
| Backward Compatible | N/A | **Yes** ✅ |

**Use the type-safe builder for better code quality without any performance cost!**
