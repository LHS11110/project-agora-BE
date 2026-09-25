#pragma once

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

inline std::string environmentValue(const char* name) {
    const char* configured = std::getenv(name);
    if (configured && *configured) return configured;

    const std::string file_variable = std::string(name) + "_FILE";
    const char* file_path = std::getenv(file_variable.c_str());
    if (!file_path || !*file_path) return configured ? configured : "";

    std::ifstream file(file_path, std::ios::binary);
    if (!file) return "";
    std::string value((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}
