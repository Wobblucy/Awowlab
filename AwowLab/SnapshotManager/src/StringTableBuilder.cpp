#include "StringTableBuilder.h"
#include <cstring>

namespace snapshot {

uint32_t StringTableBuilder::intern(std::string_view str) {
    if (str.empty()) {
        return 0;
    }

    auto it = index_.find(str);
    if (it != index_.end()) {
        return it->second;
    }

    uint32_t id = static_cast<uint32_t>(strings_.size() + 1);  // 0 is reserved for empty
    strings_.emplace_back(str);
    index_.emplace(strings_.back(), id);
    return id;
}

std::string_view StringTableBuilder::lookup(uint32_t id) const {
    if (id == 0 || id > strings_.size()) {
        return {};
    }
    return strings_[id - 1];
}

std::vector<uint8_t> StringTableBuilder::serialize() const {
    std::vector<uint8_t> result;

    size_t totalSize = sizeof(StringTableHeader);
    for (const auto& str : strings_) {
        totalSize += 2 + str.size();
    }
    result.reserve(totalSize);

    // Header is filled in once the size is known
    result.resize(sizeof(StringTableHeader));

    for (const auto& str : strings_) {
        uint16_t len = static_cast<uint16_t>(str.size());
        result.push_back(static_cast<uint8_t>(len & 0xFF));
        result.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        // no null terminator
        result.insert(result.end(), str.begin(), str.end());
    }

    StringTableHeader header{};
    header.entry_count = static_cast<uint32_t>(strings_.size());
    header.total_bytes = static_cast<uint32_t>(result.size() - sizeof(StringTableHeader));
    std::memcpy(result.data(), &header, sizeof(header));

    return result;
}

void StringTableBuilder::clear() {
    strings_.clear();
    index_.clear();
}

} // namespace snapshot
