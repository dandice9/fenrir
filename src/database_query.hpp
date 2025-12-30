#pragma once

#include <libpq-fe.h>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>
#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>

namespace fenrir {

    // ============================================================================
    // Query Builder State Tracking (Compile-Time)
    // ============================================================================
    
    // Query type tags for compile-time validation
    struct select_query_tag {};
    struct insert_query_tag {};
    struct update_query_tag {};
    struct delete_query_tag {};
    struct no_query_tag {};
    
    // State flags for query building
    template<typename QueryType = no_query_tag,
             bool HasFrom = false,
             bool HasWhere = false,
             bool HasSet = false,
             bool HasValues = false>
    struct query_state {
        using query_type = QueryType;
        static constexpr bool has_from = HasFrom;
        static constexpr bool has_where = HasWhere;
        static constexpr bool has_set = HasSet;
        static constexpr bool has_values = HasValues;
    };
    
    // Default initial state
    using initial_query_state = query_state<>;
    
    // Concepts for compile-time validation
    template<typename State>
    concept SelectQuery = std::same_as<typename State::query_type, select_query_tag>;
    
    template<typename State>
    concept InsertQuery = std::same_as<typename State::query_type, insert_query_tag>;
    
    template<typename State>
    concept UpdateQuery = std::same_as<typename State::query_type, update_query_tag>;
    
    template<typename State>
    concept DeleteQuery = std::same_as<typename State::query_type, delete_query_tag>;
    
    template<typename State>
    concept HasFrom = State::has_from;
    
    template<typename State>
    concept HasWhere = State::has_where;
    
    template<typename State>
    concept HasSet = State::has_set;
    
    template<typename State>
    concept HasValues = State::has_values;
    
    template<typename State>
    concept NoQueryStarted = std::same_as<typename State::query_type, no_query_tag>;
    
    template<typename State>
    concept QueryStarted = !NoQueryStarted<State>;
    
    template<typename State>
    concept CanExecute = (SelectQuery<State> && HasFrom<State>) ||
                         (InsertQuery<State> && HasValues<State>) ||
                         (UpdateQuery<State> && HasSet<State>) ||
                         (DeleteQuery<State> && HasFrom<State>);
    
    template<typename State>
    concept CanAddFrom = (SelectQuery<State> || DeleteQuery<State>) && !HasFrom<State>;
    
    template<typename State>
    concept CanAddWhere = QueryStarted<State> && HasFrom<State> && !HasWhere<State>;
    
    template<typename State>
    concept CanAddSet = UpdateQuery<State> && !HasSet<State>;
    
    template<typename State>
    concept CanAddValues = InsertQuery<State> && !HasValues<State>;

    // RAII wrapper for PGresult
    class query_result {
    public:
        explicit query_result(PGresult* result) : result_(result, PQclear) {}

        query_result(const query_result&) = delete;
        query_result& operator=(const query_result&) = delete;
        query_result(query_result&&) = default;
        query_result& operator=(query_result&&) = default;

        [[nodiscard]] int row_count() const noexcept {
            return result_ ? PQntuples(result_.get()) : 0;
        }

        [[nodiscard]] int column_count() const noexcept {
            return result_ ? PQnfields(result_.get()) : 0;
        }

        [[nodiscard]] std::optional<std::string> column_name(int col) const {
            if (!result_ || col < 0 || col >= column_count()) {
                return std::nullopt;
            }
            return PQfname(result_.get(), col);
        }

        [[nodiscard]] std::optional<int> column_index(std::string_view name) const {
            if (!result_) return std::nullopt;
            int idx = PQfnumber(result_.get(), name.data());
            return idx >= 0 ? std::optional<int>(idx) : std::nullopt;
        }

        [[nodiscard]] bool is_null(int row, int col) const noexcept {
            if (!result_) return true;
            return PQgetisnull(result_.get(), row, col) == 1;
        }

        [[nodiscard]] std::optional<std::string_view> get_value(int row, int col) const {
            if (!result_ || is_null(row, col)) {
                return std::nullopt;
            }
            return std::string_view(PQgetvalue(result_.get(), row, col));
        }

        [[nodiscard]] std::optional<std::string_view> get_value(int row, std::string_view col_name) const {
            auto idx = column_index(col_name);
            if (!idx) return std::nullopt;
            return get_value(row, *idx);
        }

        // Get value with type conversion
        template<typename T>
        [[nodiscard]] std::optional<T> get(int row, int col) const {
            auto val = get_value(row, col);
            if (!val) return std::nullopt;
            return parse_value<T>(*val);
        }

        template<typename T>
        [[nodiscard]] std::optional<T> get(int row, std::string_view col_name) const {
            auto idx = column_index(col_name);
            if (!idx) return std::nullopt;
            return get<T>(row, *idx);
        }

        // Get number of affected rows (for INSERT/UPDATE/DELETE)
        [[nodiscard]] int affected_rows() const noexcept {
            if (!result_) return 0;
            const char* rows = PQcmdTuples(result_.get());
            return rows && *rows ? std::atoi(rows) : 0;
        }

        // Iterator support for range-based for loops
        class row_iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = int;
            using difference_type = std::ptrdiff_t;
            using pointer = const int*;
            using reference = const int&;

            row_iterator(const query_result* result, int row) 
                : result_(result), row_(row) {}

            int operator*() const { return row_; }
            
            row_iterator& operator++() {
                ++row_;
                return *this;
            }

            row_iterator operator++(int) {
                row_iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const row_iterator& other) const {
                return row_ == other.row_;
            }

            bool operator!=(const row_iterator& other) const {
                return !(*this == other);
            }

        private:
            const query_result* result_;
            int row_;
        };

        [[nodiscard]] row_iterator begin() const {
            return row_iterator(this, 0);
        }

        [[nodiscard]] row_iterator end() const {
            return row_iterator(this, row_count());
        }

        // Access to raw result
        [[nodiscard]] PGresult* native_handle() const noexcept {
            return result_.get();
        }

    private:
        template<typename T>
        static std::optional<T> parse_value(std::string_view str) {
            if constexpr (std::is_same_v<T, std::string>) {
                return std::string(str);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                return str;
            } else if constexpr (std::is_same_v<T, int>) {
                try {
                    return std::stoi(std::string(str));
                } catch (...) {
                    return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, long>) {
                try {
                    return std::stol(std::string(str));
                } catch (...) {
                    return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, long long>) {
                try {
                    return std::stoll(std::string(str));
                } catch (...) {
                    return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, float>) {
                try {
                    return std::stof(std::string(str));
                } catch (...) {
                    return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, double>) {
                try {
                    return std::stod(std::string(str));
                } catch (...) {
                    return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, bool>) {
                return str == "t" || str == "true" || str == "1";
            }
            return std::nullopt;
        }

        std::unique_ptr<PGresult, decltype(&PQclear)> result_;
    };

    // ============================================================================
    // Type-Safe Query Builder with Compile-Time Validation
    // Safe version that can work with both references and shared pointers
    // ============================================================================
    
    template<typename State = initial_query_state>
    class typed_query_builder {
    public:
        // Constructor taking reference (caller must ensure connection outlives query)
        explicit typed_query_builder(database_connection& conn) 
            : conn_ptr_(nullptr), conn_ref_(&conn) {}
        
        // Constructor taking shared_ptr (safer for async operations)
        explicit typed_query_builder(std::shared_ptr<database_connection> conn)
            : conn_ptr_(std::move(conn)), conn_ref_(nullptr) {}
        
        // Copy state for chaining (reference version)
        typed_query_builder(database_connection& conn, std::string query)
            : conn_ptr_(nullptr), conn_ref_(&conn), query_(std::move(query)) {}

        // Copy state for chaining (shared_ptr version)
        typed_query_builder(std::shared_ptr<database_connection> conn, std::string query)
            : conn_ptr_(std::move(conn)), conn_ref_(nullptr), query_(std::move(query)) {}
        
        // ========================================================================
        // Query Starters (only allowed when no query started)
        // ========================================================================
        
        // Start SELECT query
        [[nodiscard]] auto select(std::string_view columns) requires NoQueryStarted<State> {
            using new_state = query_state<select_query_tag, false, false, false, false>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_, 
                    std::format("SELECT {}", columns)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_, 
                std::format("SELECT {}", columns)
            );
        }
        
        // Start INSERT query
        [[nodiscard]] auto insert_into(std::string_view table, std::string_view columns) 
            requires NoQueryStarted<State> {
            using new_state = query_state<insert_query_tag, false, false, false, false>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_,
                    std::format("INSERT INTO {} ({})", table, columns)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_,
                std::format("INSERT INTO {} ({})", table, columns)
            );
        }
        
        // Start UPDATE query
        [[nodiscard]] auto update(std::string_view table) requires NoQueryStarted<State> {
            using new_state = query_state<update_query_tag, false, false, false, false>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_,
                    std::format("UPDATE {}", table)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_,
                std::format("UPDATE {}", table)
            );
        }
        
        // Start DELETE query
        [[nodiscard]] auto delete_from(std::string_view table) requires NoQueryStarted<State> {
            using new_state = query_state<delete_query_tag, true, false, false, false>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_,
                    std::format("DELETE FROM {}", table)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_,
                std::format("DELETE FROM {}", table)
            );
        }
        
        // ========================================================================
        // FROM clause (SELECT and DELETE only, once per query)
        // ========================================================================
        
        [[nodiscard]] auto from(std::string_view table) requires CanAddFrom<State> {
            using new_state = query_state<typename State::query_type, true, 
                                         State::has_where, State::has_set, State::has_values>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_,
                    query_ + std::format(" FROM {}", table)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_,
                query_ + std::format(" FROM {}", table)
            );
        }
        
        // ========================================================================
        // SET clause (UPDATE only, required before WHERE)
        // ========================================================================
        
        [[nodiscard]] auto set(std::string_view assignments) requires CanAddSet<State> {
            using new_state = query_state<typename State::query_type, true,
                                         State::has_where, true, State::has_values>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_,
                    query_ + std::format(" SET {}", assignments)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_,
                query_ + std::format(" SET {}", assignments)
            );
        }
        
        // ========================================================================
        // VALUES clause (INSERT only, required)
        // ========================================================================
        
        [[nodiscard]] auto values(std::string_view value_list) requires CanAddValues<State> {
            using new_state = query_state<typename State::query_type, true,
                                         State::has_where, State::has_set, true>;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(
                    conn_ptr_,
                    query_ + std::format(" VALUES ({})", value_list)
                );
            }
            return typed_query_builder<new_state>(
                *conn_ref_,
                query_ + std::format(" VALUES ({})", value_list)
            );
        }

        // ========================================================================
        // WHERE clause (can be added multiple times as AND conditions)
        // ========================================================================
        
        [[nodiscard]] auto where(std::string_view condition) 
            requires (QueryStarted<State> && HasFrom<State>) {
            using new_state = query_state<typename State::query_type, State::has_from,
                                         true, State::has_set, State::has_values>;
            std::string new_query = query_;
            if (!State::has_where) {
                new_query += " WHERE ";
            } else {
                new_query += " AND ";
            }
            new_query += condition;
            if (conn_ptr_) {
                return typed_query_builder<new_state>(conn_ptr_, std::move(new_query));
            }
            return typed_query_builder<new_state>(*conn_ref_, std::move(new_query));
        }
        
        // ========================================================================
        // ORDER BY (SELECT only, after FROM)
        // ========================================================================
        
        [[nodiscard]] auto order_by(std::string_view column, bool ascending = true)
            requires (SelectQuery<State> && HasFrom<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" ORDER BY {} {}", column, ascending ? "ASC" : "DESC")
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" ORDER BY {} {}", column, ascending ? "ASC" : "DESC")
            );
        }
        
        // ========================================================================
        // LIMIT and OFFSET (SELECT only)
        // ========================================================================
        
        [[nodiscard]] auto limit(int count) requires (SelectQuery<State> && HasFrom<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" LIMIT {}", count)
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" LIMIT {}", count)
            );
        }
        
        [[nodiscard]] auto offset(int count) requires (SelectQuery<State> && HasFrom<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" OFFSET {}", count)
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" OFFSET {}", count)
            );
        }
        
        // ========================================================================
        // JOIN (SELECT only, after FROM)
        // ========================================================================
        
        [[nodiscard]] auto join(std::string_view table, std::string_view condition, 
                               std::string_view type = "INNER")
            requires (SelectQuery<State> && HasFrom<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" {} JOIN {} ON {}", type, table, condition)
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" {} JOIN {} ON {}", type, table, condition)
            );
        }
        
        [[nodiscard]] auto inner_join(std::string_view table, std::string_view condition)
            requires (SelectQuery<State> && HasFrom<State>) {
            return join(table, condition, "INNER");
        }
        
        [[nodiscard]] auto left_join(std::string_view table, std::string_view condition)
            requires (SelectQuery<State> && HasFrom<State>) {
            return join(table, condition, "LEFT");
        }
        
        [[nodiscard]] auto right_join(std::string_view table, std::string_view condition)
            requires (SelectQuery<State> && HasFrom<State>) {
            return join(table, condition, "RIGHT");
        }
        
        [[nodiscard]] auto full_join(std::string_view table, std::string_view condition)
            requires (SelectQuery<State> && HasFrom<State>) {
            return join(table, condition, "FULL");
        }
        
        // ========================================================================
        // GROUP BY and HAVING (SELECT only)
        // ========================================================================
        
        [[nodiscard]] auto group_by(std::string_view columns)
            requires (SelectQuery<State> && HasFrom<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" GROUP BY {}", columns)
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" GROUP BY {}", columns)
            );
        }
        
        [[nodiscard]] auto having(std::string_view condition)
            requires (SelectQuery<State> && HasFrom<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" HAVING {}", condition)
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" HAVING {}", condition)
            );
        }
        
        // ========================================================================
        // RETURNING clause (INSERT, UPDATE, DELETE)
        // ========================================================================
        
        [[nodiscard]] auto returning(std::string_view columns)
            requires (InsertQuery<State> || UpdateQuery<State> || DeleteQuery<State>) {
            if (conn_ptr_) {
                return typed_query_builder<State>(
                    conn_ptr_,
                    query_ + std::format(" RETURNING {}", columns)
                );
            }
            return typed_query_builder<State>(
                *conn_ref_,
                query_ + std::format(" RETURNING {}", columns)
            );
        }
        
        // ========================================================================
        // Execution (only allowed when query is complete)
        // ========================================================================
        
        [[nodiscard]] query_result execute() requires CanExecute<State> {
            auto& conn = get_connection();
            auto result = conn.execute(query_);
            return query_result(result);
        }
        
        template<typename... Args>
        [[nodiscard]] query_result execute(Args&&... args) requires CanExecute<State> {
            auto& conn = get_connection();
            auto result = conn.execute_params(query_, std::forward<Args>(args)...);
            return query_result(result);
        }
        
        template<typename... Args>
        [[nodiscard]] net::awaitable<query_result> execute_async(Args&&... args)
            requires CanExecute<State> {
            auto& conn = get_connection();
            co_return co_await conn.async_execute_params(query_, std::forward<Args>(args)...);
        }
        
        // ========================================================================
        // Direct SQL (bypass builder validation)
        // ========================================================================
        
        [[nodiscard]] static query_result raw(database_connection& conn, std::string_view sql) {
            auto result = conn.execute(sql);
            return query_result(result);
        }
        
        template<typename... Args>
        [[nodiscard]] static query_result raw_params(database_connection& conn,
                                                     std::string_view sql, Args&&... args) {
            auto result = conn.execute_params(sql, std::forward<Args>(args)...);
            return query_result(result);
        }
        
        // ========================================================================
        // Query inspection
        // ========================================================================
        
        [[nodiscard]] const std::string& get_query() const noexcept {
            return query_;
        }
        
        [[nodiscard]] std::string_view query_type_name() const noexcept {
            if constexpr (SelectQuery<State>) return "SELECT";
            else if constexpr (InsertQuery<State>) return "INSERT";
            else if constexpr (UpdateQuery<State>) return "UPDATE";
            else if constexpr (DeleteQuery<State>) return "DELETE";
            else return "NONE";
        }

        // Check if connection is valid
        [[nodiscard]] bool has_valid_connection() const noexcept {
            if (conn_ptr_) return conn_ptr_->is_connected();
            if (conn_ref_) return conn_ref_->is_connected();
            return false;
        }
        
    private:
        // Get the underlying connection reference
        [[nodiscard]] database_connection& get_connection() {
            if (conn_ptr_) return *conn_ptr_;
            if (conn_ref_) return *conn_ref_;
            throw database_error{"No valid database connection"};
        }

        [[nodiscard]] const database_connection& get_connection() const {
            if (conn_ptr_) return *conn_ptr_;
            if (conn_ref_) return *conn_ref_;
            throw database_error{"No valid database connection"};
        }

        std::shared_ptr<database_connection> conn_ptr_;  // Owned connection (for async safety)
        database_connection* conn_ref_;                   // Non-owned reference (legacy usage)
        std::string query_;
    };
    
    // ============================================================================
    // Legacy Query Builder (backward compatible, no compile-time checks)
    // Safe version that can work with both references and shared pointers
    // ============================================================================
    
    class database_query {
    public:
        // Constructor taking reference (caller must ensure connection outlives query)
        explicit database_query(database_connection& conn) 
            : conn_ptr_(nullptr), conn_ref_(&conn) {}
        
        // Constructor taking shared_ptr (safer for async operations)
        explicit database_query(std::shared_ptr<database_connection> conn) 
            : conn_ptr_(std::move(conn)), conn_ref_(nullptr) {}

        // Move constructor
        database_query(database_query&& other) noexcept
            : conn_ptr_(std::move(other.conn_ptr_)),
              conn_ref_(other.conn_ref_),
              query_(std::move(other.query_)) {
            other.conn_ref_ = nullptr;
        }

        // Move assignment
        database_query& operator=(database_query&& other) noexcept {
            if (this != &other) {
                conn_ptr_ = std::move(other.conn_ptr_);
                conn_ref_ = other.conn_ref_;
                query_ = std::move(other.query_);
                other.conn_ref_ = nullptr;
            }
            return *this;
        }

        // Disable copy (connection ownership is complex)
        database_query(const database_query&) = delete;
        database_query& operator=(const database_query&) = delete;

        // Build and execute query
        database_query& select(std::string_view columns) {
            query_ = std::format("SELECT {}", columns);
            return *this;
        }

        database_query& insert_into(std::string_view table, std::string_view columns) {
            query_ = std::format("INSERT INTO {} ({})", table, columns);
            return *this;
        }

        database_query& update(std::string_view table) {
            query_ = std::format("UPDATE {}", table);
            return *this;
        }

        database_query& set(std::string_view assignments) {
            query_ += std::format(" SET {}", assignments);
            return *this;
        }

        database_query& delete_from(std::string_view table) {
            query_ = std::format("DELETE FROM {}", table);
            return *this;
        }

        database_query& from(std::string_view table) {
            query_ += std::format(" FROM {}", table);
            return *this;
        }

        database_query& where(std::string_view condition) {
            if(condition.empty()) {
                return *this;
            }

            if (query_.find(" WHERE ") == std::string::npos) {
                query_ += " WHERE ";
            } else {
                query_ += " AND ";
            }
            query_ += condition;
            return *this;
        }

        database_query& where(std::string_view condition, auto&&... params) {
            if(condition.empty()) {
                return *this;
            }

            if (query_.find(" WHERE ") == std::string::npos) {
                query_ += " WHERE ";
            } else {
                query_ += " AND ";
            }
            query_ += condition;
            // Parameters can be bound during execution
            return *this;
        }

        database_query& order_by(std::string_view column, bool ascending = true) {
            query_ += std::format(" ORDER BY {} {}", column, ascending ? "ASC" : "DESC");
            return *this;
        }

        database_query& limit(int count) {
            query_ += std::format(" LIMIT {}", count);
            return *this;
        }

        database_query& offset(int count) {
            query_ += std::format(" OFFSET {}", count);
            return *this;
        }

        database_query& join(std::string_view table, std::string_view condition, std::string_view type = "INNER") {
            query_ += std::format(" {} JOIN {} ON {}", type, table, condition);
            return *this;
        }

        database_query& group_by(std::string_view columns) {
            query_ += std::format(" GROUP BY {}", columns);
            return *this;
        }

        // Execute the built query
        [[nodiscard]] query_result execute() {
            auto& conn = get_connection();
            auto result = conn.execute(query_);
            return query_result(result);
        }

        // Execute with parameters
        template<typename... Args>
        [[nodiscard]] query_result execute(Args&&... args) {
            auto& conn = get_connection();
            auto result = conn.execute_params(query_, std::forward<Args>(args)...);
            return query_result(result);
        }

        // Execute with parameters asynchronously
        template<typename... Args>
        [[nodiscard]] net::awaitable<query_result> execute_async(Args&&... args) {
            auto& conn = get_connection();
            auto result = co_await conn.async_execute_params(
                query_, std::forward<Args>(args)...);
            co_return result;
        }

        // Direct SQL execution
        [[nodiscard]] query_result raw(std::string_view sql) {
            auto& conn = get_connection();
            auto result = conn.execute(sql);
            return query_result(result);
        }

        // Parameterized SQL execution
        template<typename... Args>
        [[nodiscard]] query_result raw_params(
            std::string_view sql, Args&&... args) {
            auto& conn = get_connection();
            auto result = conn.execute_params(sql, std::forward<Args>(args)...);
            return query_result(result);
        }

        // Get the current query string
        [[nodiscard]] const std::string& get_query() const noexcept {
            return query_;
        }

        // Reset query builder
        database_query& reset() {
            query_.clear();
            return *this;
        }

        // Check if connection is valid
        [[nodiscard]] bool has_valid_connection() const noexcept {
            if (conn_ptr_) return conn_ptr_->is_connected();
            if (conn_ref_) return conn_ref_->is_connected();
            return false;
        }

    private:
        // Get the underlying connection reference
        [[nodiscard]] database_connection& get_connection() {
            if (conn_ptr_) return *conn_ptr_;
            if (conn_ref_) return *conn_ref_;
            throw database_error{"No valid database connection"};
        }

        [[nodiscard]] const database_connection& get_connection() const {
            if (conn_ptr_) return *conn_ptr_;
            if (conn_ref_) return *conn_ref_;
            throw database_error{"No valid database connection"};
        }

        std::shared_ptr<database_connection> conn_ptr_;  // Owned connection (for async safety)
        database_connection* conn_ref_;                   // Non-owned reference (legacy usage)
        std::string query_;
    };

} // namespace fenrir