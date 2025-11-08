#pragma once

#include "database_connection.hpp"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <semaphore>
#include <functional>

namespace fenrir {

    // RAII connection handle from pool
    class pooled_connection {
    public:
        pooled_connection() = default;
        
        pooled_connection(std::unique_ptr<database_connection> conn,
                         std::function<void(std::unique_ptr<database_connection>)> returner)
            : conn_(std::move(conn)), returner_(std::move(returner)) {}

        ~pooled_connection() {
            if (conn_ && returner_) {
                returner_(std::move(conn_));
            }
        }

        // Disable copy, enable move
        pooled_connection(const pooled_connection&) = delete;
        pooled_connection& operator=(const pooled_connection&) = delete;
        
        pooled_connection(pooled_connection&& other) noexcept
            : conn_(std::move(other.conn_)), returner_(std::move(other.returner_)) {}
        
        pooled_connection& operator=(pooled_connection&& other) noexcept {
            if (this != &other) {
                if (conn_ && returner_) {
                    returner_(std::move(conn_));
                }
                conn_ = std::move(other.conn_);
                returner_ = std::move(other.returner_);
            }
            return *this;
        }

        database_connection* operator->() { return conn_.get(); }
        const database_connection* operator->() const { return conn_.get(); }
        database_connection& operator*() { return *conn_; }
        const database_connection& operator*() const { return *conn_; }

        [[nodiscard]] bool valid() const noexcept { return conn_ != nullptr; }
        explicit operator bool() const noexcept { return valid(); }

    private:
        std::unique_ptr<database_connection> conn_;
        std::function<void(std::unique_ptr<database_connection>)> returner_;
    };

    // Thread-safe connection pool
    class database_pool {
    public:
        struct pool_config {
            std::string connection_string;
            database_connection::connection_params connection_params;
            size_t min_connections = 2;
            size_t max_connections = 10;
            std::chrono::seconds connection_timeout{30};
            std::chrono::seconds idle_timeout{300};
            bool validate_on_acquire = true;
            bool use_connection_string = true;
        };

        explicit database_pool(const pool_config& config)
            : config_(config),
              active_connections_(0),
              shutdown_(false) {
            
            if (config_.min_connections > config_.max_connections) {
                throw database_error{"min_connections cannot exceed max_connections"};
            }

            // Create minimum connections
            for (size_t i = 0; i < config_.min_connections; ++i) {
                try {
                    available_connections_.push(create_connection());
                } catch (const database_error& e) {
                    // Clean up and rethrow
                    shutdown();
                    throw;
                }
            }
        }

        ~database_pool() {
            shutdown();
        }

        // Disable copy and move
        database_pool(const database_pool&) = delete;
        database_pool& operator=(const database_pool&) = delete;
        database_pool(database_pool&&) = delete;
        database_pool& operator=(database_pool&&) = delete;

        // Acquire connection from pool
        [[nodiscard]] std::expected<pooled_connection, database_error> acquire(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
            
            using namespace std::chrono;
            auto deadline = steady_clock::now() + timeout;

            std::unique_lock<std::mutex> lock(mutex_);

            while (true) {
                if (shutdown_) {
                    return std::unexpected(database_error{"Pool is shutting down"});
                }

                // Try to get available connection
                if (!available_connections_.empty()) {
                    auto conn = std::move(available_connections_.front());
                    available_connections_.pop();
                    
                    // Validate connection if configured
                    if (config_.validate_on_acquire && !conn->is_connected()) {
                        try {
                            conn->reset();
                        } catch (...) {
                            // Create new connection if reset fails
                            conn = create_connection();
                        }
                    }

                    ++active_connections_;
                    
                    // Return with custom deleter that returns to pool
                    return pooled_connection(
                        std::move(conn),
                        [this](std::unique_ptr<database_connection> c) {
                            this->return_connection(std::move(c));
                        }
                    );
                }

                // Try to create new connection if under max limit
                size_t total = active_connections_ + available_connections_.size();
                if (total < config_.max_connections) {
                    try {
                        auto conn = create_connection();
                        ++active_connections_;
                        
                        return pooled_connection(
                            std::move(conn),
                            [this](std::unique_ptr<database_connection> c) {
                                this->return_connection(std::move(c));
                            }
                        );
                    } catch (const database_error& e) {
                        return std::unexpected(e);
                    }
                }

                // Wait for connection to become available
                auto now = steady_clock::now();
                if (now >= deadline) {
                    return std::unexpected(database_error{"Timeout waiting for connection"});
                }

                cv_.wait_until(lock, deadline);
            }
        }

        // Get pool statistics
        struct pool_stats {
            size_t active_connections;
            size_t available_connections;
            size_t total_connections;
            size_t max_connections;
        };

        [[nodiscard]] pool_stats get_stats() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return pool_stats{
                .active_connections = active_connections_,
                .available_connections = available_connections_.size(),
                .total_connections = active_connections_ + available_connections_.size(),
                .max_connections = config_.max_connections
            };
        }

        // Drain pool and close all connections
        void shutdown() {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
            
            // Clear all available connections
            while (!available_connections_.empty()) {
                available_connections_.pop();
            }
            
            cv_.notify_all();
        }

        [[nodiscard]] bool is_shutdown() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return shutdown_;
        }

    private:
        std::unique_ptr<database_connection> create_connection() {
            if (config_.use_connection_string) {
                return std::make_unique<database_connection>(config_.connection_string);
            } else {
                return std::make_unique<database_connection>(config_.connection_params);
            }
        }

        void return_connection(std::unique_ptr<database_connection> conn) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (shutdown_) {
                return; // Discard connection if shutting down
            }

            --active_connections_;
            
            // Check if connection is still valid
            if (conn && conn->is_connected()) {
                available_connections_.push(std::move(conn));
                cv_.notify_one();
            } else {
                // Connection is dead, don't return it to pool
                cv_.notify_one();
            }
        }

        pool_config config_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::queue<std::unique_ptr<database_connection>> available_connections_;
        size_t active_connections_;
        bool shutdown_;
    };

} // namespace fenrir