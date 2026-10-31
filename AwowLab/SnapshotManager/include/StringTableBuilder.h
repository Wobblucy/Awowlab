#pragma once
#include "SnapshotFormat.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace snapshot {

// Transparent hash and equality let index_ be searched with a string_view
struct StringHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }

    size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct StringEqual {
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

// Interns the strings (GUIDs, names) an export references. Ids start at 1;
// 0 means an empty string.
class StringTableBuilder {
public:
    StringTableBuilder() = default;

    uint32_t intern(std::string_view str);

    std::string_view lookup(uint32_t id) const;

    size_t size() const { return strings_.size(); }

    bool empty() const { return strings_.empty(); }

    // [StringTableHeader] then per string a little-endian uint16 length and the bytes
    std::vector<uint8_t> serialize() const;

    void clear();

private:
    std::vector<std::string> strings_;  // id - 1 -> string
    std::unordered_map<std::string, uint32_t, StringHash, StringEqual> index_;
};

} // namespace snapshot
