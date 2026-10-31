#pragma once

#include <rapidjson/document.h>
#include <string>
#include <cstdint>
#include <optional>
#include <charconv>
#include <system_error>

namespace warcraftlogs {
namespace json {

// Lenient readers for WarcraftLogs JSON fields: a missing or mistyped field
// gives the default.
inline int32_t getInt(const rapidjson::Value& obj, const char* key, int32_t defaultVal = 0) {
    if (obj.HasMember(key) && obj[key].IsInt()) {
        return obj[key].GetInt();
    }
    return defaultVal;
}

inline uint32_t getSpellId(const rapidjson::Value& obj, const char* key = "abilityGameID") {
    if (obj.HasMember(key)) {
        const auto& val = obj[key];
        if (val.IsInt()) return static_cast<uint32_t>(val.GetInt());
        if (val.IsUint()) return val.GetUint();
        if (val.IsInt64()) return static_cast<uint32_t>(val.GetInt64());
    }
    return 0;
}

inline int64_t getInt64(const rapidjson::Value& obj, const char* key, int64_t defaultVal = 0) {
    if (obj.HasMember(key)) {
        const auto& val = obj[key];
        if (val.IsInt64()) return val.GetInt64();
        if (val.IsInt()) return val.GetInt();
        if (val.IsUint64()) return static_cast<int64_t>(val.GetUint64());
        if (val.IsUint()) return val.GetUint();
    }
    return defaultVal;
}

// Negative values read as 0.
inline int64_t getSanitizedInt64(const rapidjson::Value& obj, const char* key, int64_t defaultVal = 0) {
    int64_t val = getInt64(obj, key, defaultVal);
    return val < 0 ? 0 : val;
}

inline uint64_t getUint64(const rapidjson::Value& obj, const char* key, uint64_t defaultVal = 0) {
    if (obj.HasMember(key)) {
        const auto& val = obj[key];
        if (val.IsUint64()) return val.GetUint64();
        if (val.IsUint()) return val.GetUint();
        if (val.IsInt64() && val.GetInt64() >= 0) return static_cast<uint64_t>(val.GetInt64());
        if (val.IsInt() && val.GetInt() >= 0) return static_cast<uint64_t>(val.GetInt());
    }
    return defaultVal;
}

inline std::string getString(const rapidjson::Value& obj, const char* key, const std::string& defaultVal = "") {
    if (obj.HasMember(key) && obj[key].IsString()) {
        return std::string(obj[key].GetString(), obj[key].GetStringLength());
    }
    return defaultVal;
}

inline bool getBool(const rapidjson::Value& obj, const char* key, bool defaultVal = false) {
    if (obj.HasMember(key) && obj[key].IsBool()) {
        return obj[key].GetBool();
    }
    return defaultVal;
}

// A millisecond timestamp, which the API sends as an integer or a double.
// nullopt when the field is missing, null or not a number.
inline std::optional<int64_t> getTimestamp(const rapidjson::Value& obj, const char* key) {
    if (!obj.HasMember(key)) {
        return std::nullopt;
    }
    const auto& val = obj[key];
    if (val.IsInt64()) return val.GetInt64();
    if (val.IsDouble()) return static_cast<int64_t>(val.GetDouble());
    return std::nullopt;
}

// An actor id written as an object key ("12": "Name"). nullopt for keys that
// are not a whole int32.
inline std::optional<int32_t> parseIdKey(const rapidjson::Value& key) {
    if (!key.IsString()) {
        return std::nullopt;
    }
    const char* first = key.GetString();
    const char* last = first + key.GetStringLength();
    int32_t id = 0;
    auto [ptr, ec] = std::from_chars(first, last, id);
    if (ec != std::errc{} || ptr != last || first == last) {
        return std::nullopt;
    }
    return id;
}

} // namespace json
} // namespace warcraftlogs
