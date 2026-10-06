#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/** A SQL statement and values kept separately for sp_executesql. */
class SqlCommand {
public:
    enum class ParameterType {
        Varchar,
        UnicodeText,
        UnicodeMaxText,
        Int32,
        Int64
    };

    struct Parameter {
        std::string name;
        ParameterType type;
        std::string text_value;
        std::int32_t int_value = 0;
        std::int64_t int64_value = 0;
    };

    explicit SqlCommand(std::string statement) : statement_(std::move(statement)) {}

    SqlCommand& addText(std::string name, std::string value) {
        parameters_.push_back({std::move(name), ParameterType::UnicodeText, std::move(value), 0});
        return *this;
    }

    SqlCommand& addMaxText(std::string name, std::string value) {
        parameters_.push_back({std::move(name), ParameterType::UnicodeMaxText, std::move(value), 0});
        return *this;
    }

    SqlCommand& addVarchar(std::string name, std::string value) {
        parameters_.push_back({std::move(name), ParameterType::Varchar, std::move(value), 0});
        return *this;
    }

    SqlCommand& addInt(std::string name, std::int32_t value) {
        parameters_.push_back({std::move(name), ParameterType::Int32, {}, value});
        return *this;
    }

    SqlCommand& addInt64(std::string name, std::int64_t value) {
        parameters_.push_back({std::move(name), ParameterType::Int64, {}, 0, value});
        return *this;
    }

    const std::string& statement() const { return statement_; }
    const std::vector<Parameter>& parameters() const { return parameters_; }

    std::string parameterDeclarations() const {
        std::string declarations;
        for (const auto& parameter : parameters_) {
            if (!declarations.empty()) declarations += ", ";
            declarations += parameter.name;
            switch (parameter.type) {
                case ParameterType::Varchar: declarations += " VARCHAR(4000)"; break;
                case ParameterType::UnicodeText: declarations += " NVARCHAR(4000)"; break;
                case ParameterType::UnicodeMaxText: declarations += " NVARCHAR(MAX)"; break;
                case ParameterType::Int32: declarations += " INT"; break;
                case ParameterType::Int64: declarations += " BIGINT"; break;
            }
        }
        return declarations;
    }

    std::string validationError() const {
        if (statement_.empty()) return "statement is empty";
        if (statement_.size() > kMaxStatementBytes) return "statement exceeds SQL statement limit";
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            const auto& parameter = parameters_[i];
            if (!isValidName(parameter.name)) return "invalid parameter name at index " + std::to_string(i);
            if ((parameter.type == ParameterType::Varchar || parameter.type == ParameterType::UnicodeText)
                    && parameter.text_value.size() > kMaxRpcTextBytes) {
                return "text parameter exceeds RPC text limit at index " + std::to_string(i);
            }
            if (parameter.type == ParameterType::UnicodeMaxText
                    && parameter.text_value.size() > kMaxBoundTextBytes) {
                return "MAX text parameter exceeds bound text limit at index " + std::to_string(i);
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (parameters_[j].name == parameter.name) return "duplicate parameter name at index " + std::to_string(i);
            }
        }
        const std::string declarations = parameterDeclarations();
        if (declarations.size() > kMaxRpcTextBytes) return "parameter declarations exceed RPC text limit";
        return {};
    }

    bool isValid() const { return validationError().empty(); }

    static constexpr std::size_t kMaxStatementBytes = 64 * 1024;
    static constexpr std::size_t kMaxRpcTextBytes = 4000;
    static constexpr std::size_t kMaxBoundTextBytes = 1024 * 1024;

private:
    static bool isValidName(const std::string& name) {
        if (name.size() < 2 || name.size() > 128 || name.front() != '@') return false;
        const auto isAlpha = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        };
        const auto isAlphaNumeric = [&isAlpha](char c) {
            return isAlpha(c) || (c >= '0' && c <= '9');
        };
        if (!isAlpha(name[1])) return false;
        for (std::size_t i = 2; i < name.size(); ++i) {
            if (!isAlphaNumeric(name[i])) return false;
        }
        return true;
    }

    std::string statement_;
    std::vector<Parameter> parameters_;
};
