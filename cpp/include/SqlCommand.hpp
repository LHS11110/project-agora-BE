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
        Int32
    };

    struct Parameter {
        std::string name;
        ParameterType type;
        std::string text_value;
        std::int32_t int_value = 0;
    };

    explicit SqlCommand(std::string statement) : statement_(std::move(statement)) {}

    SqlCommand& addText(std::string name, std::string value) {
        parameters_.push_back({std::move(name), ParameterType::UnicodeText, std::move(value), 0});
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
                case ParameterType::Int32: declarations += " INT"; break;
            }
        }
        return declarations;
    }

    bool isValid() const {
        if (statement_.empty() || statement_.size() > kMaxRpcTextBytes) return false;
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            const auto& parameter = parameters_[i];
            if (!isValidName(parameter.name)) return false;
            if (parameter.type != ParameterType::Int32 && parameter.text_value.size() > kMaxRpcTextBytes) return false;
            for (std::size_t j = 0; j < i; ++j) {
                if (parameters_[j].name == parameter.name) return false;
            }
        }
        const std::string declarations = parameterDeclarations();
        return declarations.size() <= kMaxRpcTextBytes;
    }

    static constexpr std::size_t kMaxRpcTextBytes = 4000;

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
