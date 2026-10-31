#pragma once
#include "SnapshotFormat.h"
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <span>

namespace snapshot {

// Reads the string table written by StringTableBuilder so ids can be turned
// back into strings during import
class StringTableReader {
public:
    StringTableReader() = default;

    // data points at the start of the string table section. On failure see getError().
    bool deserialize(const uint8_t* data, size_t availableBytes);

    bool deserialize(std::span<const uint8_t> data) {
        return deserialize(data.data(), data.size());
    }

    // id 0 gives an empty string
    std::string_view lookup(uint32_t id) const;

    // Copy, for strings that must outlive the reader
    std::string lookupCopy(uint32_t id) const;

    size_t size() const { return strings_.size(); }

    bool empty() const { return strings_.empty(); }

    size_t bytesConsumed() const { return bytesConsumed_; }

    void clear();

    const std::string& getError() const { return error_; }

private:
    std::vector<std::string> strings_;  // id - 1 -> string
    size_t bytesConsumed_ = 0;
    std::string error_;
};

} // namespace snapshot
