#pragma once

#include <optional>
#include <string>

/** Minimal key/value port for feature-specific cache repositories. */
class KeyValueStore {
public:
    virtual ~KeyValueStore() = default;
    virtual bool set(const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual bool del(const std::string& key) = 0;
};
