#pragma once
#include <string>
#include <string_view>
#include <memory>
#include <concepts>
#include <chrono>
#include <atomic>
#include <vector>
#include <optional>
#include "database_connection.hpp"
#include "database_query.hpp"

namespace fenrir {

    // Transaction isolation levels
    enum class isolation_level {
        read_uncommitted,
        read_committed,
        repeatable_read,
        serializable
    };

    // Transaction access modes
    enum class access_mode {
        read_write,
        read_only
    };

    // Savepoint RAII wrapper
    class savepoint {
    public:
        savepoint(database_connection& conn, std::string_view name)
            : conn_(conn), name_(name), released_(false) {
            auto result = conn_.execute(std::format("SAVEPOINT {}", name_));
            if (!result) {
                throw result.error();
            }
        }

        ~savepoint() {
            if (!released_) {
                // Silently rollback to savepoint on destruction if not released
                conn_.execute(std::format("ROLLBACK TO SAVEPOINT {}", name_));
            }
        }

        // Release savepoint (commit)
        void release() {
            if (!released_) {
                auto result = conn_.execute(std::format("RELEASE SAVEPOINT {}", name_));
                if (!result) {
                    throw result.error();
                }
                released_ = true;
            }
        }

        // Rollback to savepoint
        void rollback() {
            if (!released_) {
                auto result = conn_.execute(std::format("ROLLBACK TO SAVEPOINT {}", name_));
                if (!result) {
                    throw result.error();
                }
            }
        }

    private:
        database_connection& conn_;
        std::string name_;
        bool released_;
    };

    // RAII transaction wrapper
    class database_transaction {
    public:
        explicit database_transaction(
            database_connection& conn,
            isolation_level level = isolation_level::read_committed,
            access_mode mode = access_mode::read_write,
            bool deferrable = false)
            : conn_(conn), committed_(false), rolled_back_(false) {
            
            std::string isolation_str = [level]() {
                switch (level) {
                    case isolation_level::read_uncommitted:
                        return "READ UNCOMMITTED";
                    case isolation_level::read_committed:
                        return "READ COMMITTED";
                    case isolation_level::repeatable_read:
                        return "REPEATABLE READ";
                    case isolation_level::serializable:
                        return "SERIALIZABLE";
                    default:
                        return "READ COMMITTED";
                }
            }();

            std::string mode_str = mode == access_mode::read_only ? "READ ONLY" : "READ WRITE";
            std::string defer_str = deferrable ? "DEFERRABLE" : "";

            std::string begin_cmd = std::format("BEGIN TRANSACTION ISOLATION LEVEL {} {} {}",
                                                isolation_str, mode_str, defer_str);
            
            auto result = conn_.execute(begin_cmd);
            if (!result) {
                throw result.error();
            }
        }

        ~database_transaction() {
            if (!committed_ && !rolled_back_) {
                // Auto-rollback if neither commit nor rollback was called
                try {
                    rollback();
                } catch (...) {
                    // Ignore errors in destructor
                }
            }
        }

        // Disable copy, enable move
        database_transaction(const database_transaction&) = delete;
        database_transaction& operator=(const database_transaction&) = delete;
        
        database_transaction(database_transaction&& other) noexcept
            : conn_(other.conn_),
              committed_(std::exchange(other.committed_, true)),
              rolled_back_(std::exchange(other.rolled_back_, true)),
              savepoints_(std::move(other.savepoints_)) {}

        // Commit transaction
        void commit() {
            if (committed_ || rolled_back_) {
                throw database_error{"Transaction already finalized"};
            }

            auto result = conn_.execute("COMMIT");
            if (!result) {
                throw result.error();
            }
            committed_ = true;
        }

        // Rollback transaction
        void rollback() {
            if (committed_ || rolled_back_) {
                throw database_error{"Transaction already finalized"};
            }

            auto result = conn_.execute("ROLLBACK");
            if (!result) {
                throw result.error();
            }
            rolled_back_ = true;
        }

        // Create a savepoint
        [[nodiscard]] savepoint create_savepoint(std::string_view name) {
            if (committed_ || rolled_back_) {
                throw database_error{"Cannot create savepoint in finalized transaction"};
            }
            return savepoint(conn_, name);
        }

        // Execute query within transaction
        [[nodiscard]] std::expected<query_result, database_error> execute(std::string_view sql) {
            if (committed_ || rolled_back_) {
                return std::unexpected(database_error{"Transaction already finalized"});
            }

            auto result = conn_.execute(sql);
            if (!result) {
                return std::unexpected(result.error());
            }
            return query_result(*result);
        }

        // Execute parameterized query within transaction
        template<typename... Args>
        [[nodiscard]] std::expected<query_result, database_error> execute_params(
            std::string_view sql, Args&&... args) {
            
            if (committed_ || rolled_back_) {
                return std::unexpected(database_error{"Transaction already finalized"});
            }

            auto result = conn_.execute_params(sql, std::forward<Args>(args)...);
            if (!result) {
                return std::unexpected(result.error());
            }
            return query_result(*result);
        }

        // Check transaction state
        [[nodiscard]] bool is_active() const noexcept {
            return !committed_ && !rolled_back_;
        }

        [[nodiscard]] bool is_committed() const noexcept {
            return committed_;
        }

        [[nodiscard]] bool is_rolled_back() const noexcept {
            return rolled_back_;
        }

        // Get underlying connection
        [[nodiscard]] database_connection& connection() noexcept {
            return conn_;
        }

    private:
        database_connection& conn_;
        bool committed_;
        bool rolled_back_;
        std::vector<std::string> savepoints_;
    };

    // Scoped transaction helper with automatic rollback on exception
    template<typename Func>
    requires std::invocable<Func, database_transaction&>
    auto with_transaction(
        database_connection& conn,
        Func&& func,
        isolation_level level = isolation_level::read_committed) {
        
        database_transaction txn(conn, level);
        
        try {
            if constexpr (std::is_void_v<std::invoke_result_t<Func, database_transaction&>>) {
                func(txn);
                txn.commit();
            } else {
                auto result = func(txn);
                txn.commit();
                return result;
            }
        } catch (...) {
            if (txn.is_active()) {
                txn.rollback();
            }
            throw;
        }
    }

} // namespace fenrir