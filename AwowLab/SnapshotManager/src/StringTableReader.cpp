#include "StringTableReader.h"
#include <algorithm>
#include <cstring>

namespace snapshot {

bool StringTableReader::deserialize(const uint8_t* data, size_t availableBytes) {
    clear();

    if (availableBytes < sizeof(StringTableHeader)) {
        error_ = "String table too small for header";
        return false;
    }

    StringTableHeader header{};
    std::memcpy(&header, data, sizeof(header));

    size_t expectedSize = sizeof(StringTableHeader) + header.total_bytes;
    if (availableBytes < expectedSize) {
        error_ = "String table truncated: expected " + std::to_string(expectedSize) +
                 " bytes, have " + std::to_string(availableBytes);
        return false;
    }

    // Every entry takes at least its two length bytes, so don't trust a count
    // the table can't hold when reserving
    strings_.reserve((std::min)(static_cast<size_t>(header.entry_count),
                                static_cast<size_t>(header.total_bytes) / 2));

    const uint8_t* ptr = data + sizeof(StringTableHeader);
    const uint8_t* end = data + expectedSize;

    for (uint32_t i = 0; i < header.entry_count; ++i) {
        if (ptr + 2 > end) {
            error_ = "String table truncated while reading string " + std::to_string(i);
            return false;
        }

        uint16_t len = static_cast<uint16_t>(ptr[0]) |
                       (static_cast<uint16_t>(ptr[1]) << 8);
        ptr += 2;

        if (ptr + len > end) {
            error_ = "String " + std::to_string(i) + " extends beyond table bounds";
            return false;
        }

        strings_.emplace_back(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
    }

    bytesConsumed_ = expectedSize;
    return true;
}

std::string_view StringTableReader::lookup(uint32_t id) const {
    if (id == 0 || id > strings_.size()) {
        return {};
    }
    return strings_[id - 1];
}

std::string StringTableReader::lookupCopy(uint32_t id) const {
    if (id == 0 || id > strings_.size()) {
        return {};
    }
    return strings_[id - 1];
}

void StringTableReader::clear() {
    strings_.clear();
    bytesConsumed_ = 0;
    error_.clear();
}

} // namespace snapshot
