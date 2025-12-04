#pragma once

#include <libpq-fe.h>
#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <variant>
#include <concepts>
#include <format>
#include <source_location>
#include <chrono>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <coroutine>

namespace fenrir {

    namespace net = boost::asio;

    // Forward declaration
    class query_result;

    // Error type for database operations
    struct database_error : public std::runtime_error {
        std::string sql_state;
        std::source_location location;
        
        database_error(std::string msg, std::string state = "", 
                      std::source_location loc = std::source_location::current())
            : std::runtime_error(msg), sql_state(std::move(state)), location(loc) {}
        
        std::string message() const { return what(); }
    };

    // Connection status enum
    enum class connection_status {
        ok,
        bad,
        started,
        made,
        awaiting_response,
        auth_ok,
        setenv,
        ssl_startup,
        needed
    };

    // C++20 concept for connection string types
    template<typename T>
    concept ConnectionString = std::convertible_to<T, std::string_view>;

    class database_connection {
    public:
        // Constructor with connection string
        template<ConnectionString T>
        explicit database_connection(T&& conn_str) {
            connect(std::forward<T>(conn_str));
        }

        // Named parameter constructor using C++20 designated initializers
        struct connection_params {
            std::string host = "localhost";
            std::string port = "5432";
            std::string database;
            std::string user;
            std::string password;
            std::chrono::seconds connect_timeout{30};
            std::string application_name = "fenrir";
            std::string client_encoding = "UTF8";
        };

        explicit database_connection(const connection_params& params) {
            auto conn_str = std::format(
                "host={} port={} dbname={} user={} password={} connect_timeout={} "
                "application_name={} client_encoding={}",
                params.host, params.port, params.database, params.user, params.password,
                params.connect_timeout.count(), params.application_name, params.client_encoding
            );
            connect(conn_str);
        }

        // Disable copy, enable move
        database_connection(const database_connection&) = delete;
        database_connection& operator=(const database_connection&) = delete;
        
        database_connection(database_connection&& other) noexcept
            : conn_(std::exchange(other.conn_, nullptr))
            , ioc_(std::exchange(other.ioc_, nullptr)) {}
        
        database_connection& operator=(database_connection&& other) noexcept {
            if (this != &other) {
                close();
                conn_ = std::exchange(other.conn_, nullptr);
                ioc_ = std::exchange(other.ioc_, nullptr);
            }
            return *this;
        }

        ~database_connection() {
            close();
        }

        // Check if connection is valid
        [[nodiscard]] bool is_connected() const noexcept {
            return conn_ && PQstatus(conn_) == CONNECTION_OK;
        }

        // Get connection status
        [[nodiscard]] connection_status status() const noexcept {
            if (!conn_) return connection_status::bad;
            return static_cast<connection_status>(PQstatus(conn_));
        }

        // Execute a simple query (no parameters)
        [[nodiscard]] PGresult* execute(std::string_view query) {
            if (!is_connected()) {
                throw database_error{"Connection is not valid"};
            }

            PGresult* result = PQexec(conn_, query.data());
            if (!result) {
                throw database_error{
                    std::format("Query execution failed: {}", PQerrorMessage(conn_))
                };
            }

            ExecStatusType status = PQresultStatus(result);
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                std::string error_msg = PQresultErrorMessage(result);
                std::string sql_state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
                PQclear(result);
                throw database_error{std::move(error_msg), std::move(sql_state)};
            }

            return result;
        }

        // Execute parameterized query
        template<typename... Args>
        [[nodiscard]] PGresult* execute_params(
            std::string_view query, Args&&... args) {
            
            if (!is_connected()) {
                throw database_error{"Connection is not valid"};
            }

            std::vector<std::string> param_values;
            std::vector<const char*> param_ptrs;
            
            (param_values.push_back(to_string(std::forward<Args>(args))), ...);
            
            for (const auto& val : param_values) {
                param_ptrs.push_back(val.c_str());
            }

            PGresult* result = PQexecParams(
                conn_,
                query.data(),
                static_cast<int>(param_ptrs.size()),
                nullptr,  // let server determine param types
                param_ptrs.data(),
                nullptr,  // text format
                nullptr,  // text format
                0         // text result format
            );

            if (!result) {
                throw database_error{
                    std::format("Parameterized query execution failed: {}", PQerrorMessage(conn_))
                };
            }

            ExecStatusType status = PQresultStatus(result);
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                std::string error_msg = PQresultErrorMessage(result);
                std::string sql_state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
                PQclear(result);
                throw database_error{std::move(error_msg), std::move(sql_state)};
            }

            return result;
        }

        // Get last error message
        [[nodiscard]] std::string last_error() const {
            return conn_ ? PQerrorMessage(conn_) : "No connection";
        }

        // Get database info
        [[nodiscard]] std::string database_name() const {
            return conn_ ? PQdb(conn_) : "";
        }

        [[nodiscard]] std::string user_name() const {
            return conn_ ? PQuser(conn_) : "";
        }

        [[nodiscard]] std::string host() const {
            return conn_ ? PQhost(conn_) : "";
        }

        [[nodiscard]] std::string port() const {
            return conn_ ? PQport(conn_) : "";
        }

        // Ping the connection
        [[nodiscard]] bool ping() const noexcept {
            if (!conn_) return false;
            PGPing status = PQping(PQdb(conn_));
            return status == PQPING_OK;
        }

        // Get raw connection pointer (use with caution)
        [[nodiscard]] PGconn* native_handle() noexcept {
            return conn_;
        }

        // Reset connection
        void reset() {
            if (conn_) {
                PQreset(conn_);
            }
        }

        // Close connection
        void close() noexcept {
            if (conn_) {
                PQfinish(conn_);
                conn_ = nullptr;
            }
        }

        // === ASYNC METHODS (require io_context) ===
        
        // Set io_context for async operations
        void set_io_context(net::io_context& ioc) noexcept {
            ioc_ = &ioc;
        }

        // Get the io_context (if set)
        [[nodiscard]] net::io_context* get_io_context() noexcept {
            return ioc_;
        }

        // Async query execution using coroutines (implementation below class definition)
        [[nodiscard]] net::awaitable<query_result> async_execute(std::string_view query);

        // Async parameterized query execution (implementation below class definition)
        template<typename... Args>
        [[nodiscard]] net::awaitable<query_result> async_execute_params(
            std::string_view query, Args&&... args);

        // Async prepare statement (implementation below class definition)
        [[nodiscard]] net::awaitable<void> async_prepare(
            std::string_view name, std::string_view query);

        // Async execute prepared statement (implementation below class definition)
        template<typename... Args>
        [[nodiscard]] net::awaitable<query_result> async_execute_prepared(
            std::string_view name, Args&&... args);

    private:
        void connect(std::string_view conn_str) {
            conn_ = PQconnectdb(conn_str.data());
            if (!is_connected()) {
                std::string error = last_error();
                close();
                throw database_error{std::format("Failed to connect to database: {}", error)};
            }
        }

        // Helper to convert arguments to strings
        template<typename T>
        static std::string to_string(T&& value) {
            using DecayedT = std::decay_t<T>;
            
            // Handle std::optional types
            if constexpr (requires { typename DecayedT::value_type; DecayedT{}.has_value(); }) {
                // This is std::optional
                if (!value.has_value()) {
                    return "NULL";
                }
                return to_string(*value);
            } else if constexpr (std::is_same_v<DecayedT, std::string>) {
                return std::forward<T>(value);
            } else if constexpr (std::is_same_v<DecayedT, const char*>) {
                return std::string(value);
            } else if constexpr (std::is_same_v<DecayedT, std::string_view>) {
                return std::string(value);
            } else if constexpr (std::is_arithmetic_v<DecayedT>) {
                return std::to_string(value);
            } else if constexpr (std::is_same_v<DecayedT, bool>) {
                return value ? "true" : "false";
            } else {
                // Try to use std::format for other types
                return std::format("{}", value);
            }
        }

        // Wait for query result asynchronously using socket-based waiting
        // This is much more efficient than polling - it waits for actual socket activity
        [[nodiscard]] net::awaitable<PGresult*> wait_for_result() {
            auto executor = co_await net::this_coro::executor;
            
            // Get the socket file descriptor from PostgreSQL connection
            auto socket_fd = PQsocket(native_handle());
            if (socket_fd < 0) {
                throw database_error{"Invalid socket from PostgreSQL connection"};
            }

            // Use tcp::socket for cross-platform socket wrapping
            // This works on both Windows (SOCKET) and POSIX (int fd)
            net::ip::tcp::socket socket(executor);
            socket.assign(net::ip::tcp::v4(), socket_fd);
            
            // RAII guard to release socket ownership back to PostgreSQL
            // PostgreSQL owns the socket, we're just borrowing it for async waiting
            struct socket_releaser {
                net::ip::tcp::socket& sock;
                ~socket_releaser() { sock.release(); }
            } releaser{socket};

            while (true) {
                // Check if result is ready (non-blocking)
                if (PQconsumeInput(native_handle()) == 0) {
                    throw database_error{
                        std::format("Failed to consume input: {}", last_error())
                    };
                }

                if (PQisBusy(native_handle()) == 0) {
                    // Result is ready
                    PGresult* result = PQgetResult(native_handle());
                    
                    if (!result) {
                        throw database_error{"Query returned no result"};
                    }

                    ExecStatusType status = PQresultStatus(result);
                    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                        std::string error_msg = PQresultErrorMessage(result);
                        std::string sql_state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
                        PQclear(result);
                        throw database_error{std::move(error_msg), std::move(sql_state)};
                    }

                    // Consume any remaining results
                    PGresult* next_result;
                    while ((next_result = PQgetResult(native_handle())) != nullptr) {
                        PQclear(next_result);
                    }

                    co_return result;
                }

                // Wait for socket to become readable - this is event-driven, not polling!
                // The coroutine suspends until there's data available on the socket
                co_await socket.async_wait(net::socket_base::wait_read, net::use_awaitable);
            }
        }

        PGconn* conn_{nullptr};
        net::io_context* ioc_{nullptr};
    };

} // namespace fenrir

// Include query_result definition before async implementations
#include "database_query.hpp"

namespace fenrir {

    // ============================================================================
    // ASYNC METHOD IMPLEMENTATIONS
    // ============================================================================
    // These are defined after query_result is fully available
    
    inline net::awaitable<query_result> database_connection::async_execute(std::string_view query) {
        if (!is_connected()) {
            throw database_error{"Connection is not valid"};
        }
        if (!ioc_) {
            throw database_error{"io_context not set. Call set_io_context() first."};
        }

        // Send query asynchronously
        if (!PQsendQuery(native_handle(), query.data())) {
            throw database_error{
                std::format("Failed to send async query: {}", last_error())
            };
        }

        // Wait for result asynchronously
        co_return query_result(co_await wait_for_result());
    }

    template<typename... Args>
    inline net::awaitable<query_result> database_connection::async_execute_params(
        std::string_view query, Args&&... args) {
        
        if (!is_connected()) {
            throw database_error{"Connection is not valid"};
        }
        if (!ioc_) {
            throw database_error{"io_context not set. Call set_io_context() first."};
        }

        std::vector<std::string> param_values;
        std::vector<const char*> param_ptrs;
        
        (param_values.push_back(to_string(std::forward<Args>(args))), ...);
        
        for (const auto& val : param_values) {
            param_ptrs.push_back(val.c_str());
        }

        // Send parameterized query asynchronously
        if (!PQsendQueryParams(
            native_handle(),
            query.data(),
            static_cast<int>(param_ptrs.size()),
            nullptr,
            param_ptrs.data(),
            nullptr,
            nullptr,
            0)) {
            throw database_error{
                std::format("Failed to send async parameterized query: {}", last_error())
            };
        }

        // Wait for result asynchronously
        co_return query_result(co_await wait_for_result());
    }

    inline net::awaitable<void> database_connection::async_prepare(
        std::string_view name, std::string_view query) {
        
        if (!is_connected()) {
            throw database_error{"Connection is not valid"};
        }
        if (!ioc_) {
            throw database_error{"io_context not set. Call set_io_context() first."};
        }

        if (!PQsendPrepare(native_handle(), name.data(), query.data(), 0, nullptr)) {
            throw database_error{
                std::format("Failed to send async prepare: {}", last_error())
            };
        }

        // Wait for prepare to complete
        PGresult* result = co_await wait_for_result();
        
        ExecStatusType status = PQresultStatus(result);
        if (status != PGRES_COMMAND_OK) {
            std::string error_msg = PQresultErrorMessage(result);
            PQclear(result);
            throw database_error{std::move(error_msg)};
        }
        
        PQclear(result);
        co_return;
    }

    template<typename... Args>
    inline net::awaitable<query_result> database_connection::async_execute_prepared(
        std::string_view name, Args&&... args) {
        
        if (!is_connected()) {
            throw database_error{"Connection is not valid"};
        }
        if (!ioc_) {
            throw database_error{"io_context not set. Call set_io_context() first."};
        }

        std::vector<std::string> param_values;
        std::vector<const char*> param_ptrs;
        
        (param_values.push_back(to_string(std::forward<Args>(args))), ...);
        
        for (const auto& val : param_values) {
            param_ptrs.push_back(val.c_str());
        }

        if (!PQsendQueryPrepared(
            native_handle(),
            name.data(),
            static_cast<int>(param_ptrs.size()),
            param_ptrs.data(),
            nullptr,
            nullptr,
            0)) {
            throw database_error{
                std::format("Failed to send async prepared query: {}", last_error())
            };
        }

        co_return query_result(co_await wait_for_result());
    }

} // namespace fenrir