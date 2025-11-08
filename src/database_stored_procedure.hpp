#pragma once

#include "database_connection.hpp"
#include "database_query.hpp"
#include <vector>
#include <string>
#include <format>

namespace fenrir {

    // Stored procedure parameter direction
    enum class param_direction {
        in,
        out,
        inout
    };

    // Stored procedure parameter
    struct procedure_param {
        std::string name;
        std::string value;
        param_direction direction = param_direction::in;
    };

    // Stored procedure wrapper
    class database_stored_procedure {
    public:
        explicit database_stored_procedure(database_connection& conn, std::string_view name)
            : conn_(conn), proc_name_(name) {}

        // Add input parameter
        template<typename T>
        database_stored_procedure& add_param(std::string_view name, const T& value) {
            params_.push_back(procedure_param{
                .name = std::string(name),
                .value = to_string(value),
                .direction = param_direction::in
            });
            return *this;
        }

        // Add output parameter
        database_stored_procedure& add_out_param(std::string_view name) {
            params_.push_back(procedure_param{
                .name = std::string(name),
                .value = "",
                .direction = param_direction::out
            });
            return *this;
        }

        // Add input/output parameter
        template<typename T>
        database_stored_procedure& add_inout_param(std::string_view name, const T& value) {
            params_.push_back(procedure_param{
                .name = std::string(name),
                .value = to_string(value),
                .direction = param_direction::inout
            });
            return *this;
        }

        // Execute stored procedure
        [[nodiscard]] std::expected<query_result, database_error> execute() {
            // Build parameter list for PostgreSQL function call
            std::vector<std::string> param_placeholders;
            std::vector<std::string> param_values;
            
            for (size_t i = 0; i < params_.size(); ++i) {
                if (params_[i].direction != param_direction::out) {
                    param_placeholders.push_back(std::format("${}", i + 1));
                    param_values.push_back(params_[i].value);
                }
            }

            std::string sql;
            if (param_placeholders.empty()) {
                sql = std::format("SELECT * FROM {}()", proc_name_);
                auto result = conn_.execute(sql);
                if (!result) {
                    return std::unexpected(result.error());
                }
                return query_result(*result);
            } else {
                sql = std::format("SELECT * FROM {}({})", 
                                 proc_name_,
                                 join_strings(param_placeholders, ", "));
                
                // Execute with parameters
                auto result = execute_with_params(sql, param_values);
                if (!result) {
                    return std::unexpected(result.error());
                }
                return query_result(*result);
            }
        }

        // Execute as a function returning single value
        template<typename T>
        [[nodiscard]] std::expected<std::optional<T>, database_error> execute_scalar() {
            auto result = execute();
            if (!result) {
                return std::unexpected(result.error());
            }

            if (result->row_count() == 0 || result->column_count() == 0) {
                return std::optional<T>{};
            }

            return result->get<T>(0, 0);
        }

        // Clear all parameters
        database_stored_procedure& clear_params() {
            params_.clear();
            return *this;
        }

        // Get procedure name
        [[nodiscard]] const std::string& name() const noexcept {
            return proc_name_;
        }

    private:
        template<typename T>
        static std::string to_string(const T& value) {
            if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
                return value;
            } else if constexpr (std::is_same_v<std::decay_t<T>, const char*>) {
                return std::string(value);
            } else if constexpr (std::is_same_v<std::decay_t<T>, std::string_view>) {
                return std::string(value);
            } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
                return std::to_string(value);
            } else if constexpr (std::is_same_v<std::decay_t<T>, bool>) {
                return value ? "true" : "false";
            } else {
                return std::format("{}", value);
            }
        }

        static std::string join_strings(const std::vector<std::string>& strings, 
                                       std::string_view delimiter) {
            if (strings.empty()) return "";
            
            std::string result = strings[0];
            for (size_t i = 1; i < strings.size(); ++i) {
                result += delimiter;
                result += strings[i];
            }
            return result;
        }

        std::expected<PGresult*, database_error> execute_with_params(
            std::string_view sql, const std::vector<std::string>& values) {
            
            std::vector<const char*> param_ptrs;
            for (const auto& val : values) {
                param_ptrs.push_back(val.c_str());
            }

            PGresult* result = PQexecParams(
                conn_.native_handle(),
                sql.data(),
                static_cast<int>(param_ptrs.size()),
                nullptr,
                param_ptrs.data(),
                nullptr,
                nullptr,
                0
            );

            if (!result) {
                return std::unexpected(database_error{
                    std::format("Stored procedure execution failed: {}", conn_.last_error())
                });
            }

            ExecStatusType status = PQresultStatus(result);
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                std::string error_msg = PQresultErrorMessage(result);
                std::string sql_state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
                PQclear(result);
                return std::unexpected(database_error{std::move(error_msg), std::move(sql_state)});
            }

            return result;
        }

        database_connection& conn_;
        std::string proc_name_;
        std::vector<procedure_param> params_;
    };

} // namespace fenrir