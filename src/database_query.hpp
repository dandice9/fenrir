#pragma once

#include <libpq-fe.h>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>
#include <concepts>
#include <ranges>
#include <span>

namespace fenrir {

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

    // Query builder with method chaining
    class database_query {
    public:
        explicit database_query(database_connection& conn) : conn_(conn) {}

        // Build and execute query
        database_query& select(std::string_view columns) {
            query_ = std::format("SELECT {}", columns);
            return *this;
        }

        database_query& from(std::string_view table) {
            query_ += std::format(" FROM {}", table);
            return *this;
        }

        database_query& where(std::string_view condition) {
            if (query_.find(" WHERE ") == std::string::npos) {
                query_ += " WHERE ";
            } else {
                query_ += " AND ";
            }
            query_ += condition;
            return *this;
        }

        database_query& where(std::string_view condition, auto&&... params) {
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

        // Execute the built query
        [[nodiscard]] query_result execute() {
            auto result = conn_.execute(query_);
            return query_result(result);
        }

        // Execute with parameters
        template<typename... Args>
        [[nodiscard]] query_result execute(Args&&... args) {
            auto result = conn_.execute_params(query_, std::forward<Args>(args)...);
            return query_result(result);
        }

        // Execute with parameters asynchronously
        template<typename... Args>
        [[nodiscard]] net::awaitable<query_result> execute_async(Args&&... args) {
            auto result = co_await conn_.async_execute_params(
                query_, std::forward<Args>(args)...);
            co_return result;
        }

        // Direct SQL execution
        [[nodiscard]] query_result raw(std::string_view sql) {
            auto result = conn_.execute(sql);
            return query_result(result);
        }

        // Parameterized SQL execution
        template<typename... Args>
        [[nodiscard]] query_result raw_params(
            std::string_view sql, Args&&... args) {
            auto result = conn_.execute_params(sql, std::forward<Args>(args)...);
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

    private:
        database_connection& conn_;
        std::string query_;
    };

} // namespace fenrir