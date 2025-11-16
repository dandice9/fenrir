#pragma once

#include "database_connection.hpp"
#include "database_query.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <coroutine>
#include <chrono>

namespace fenrir {

    namespace net = boost::asio;

    // Extension methods for async operations on database_connection
    // These are defined here to avoid circular dependency with query_result
    
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
    net::awaitable<query_result> database_connection::async_execute_params(
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
    net::awaitable<query_result> database_connection::async_execute_prepared(
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
