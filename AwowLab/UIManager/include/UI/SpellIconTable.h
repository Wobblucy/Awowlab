#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// Layout of spell_icons.bin:
//   header   magic "ICON" (u32), version (u32), numFilenames (u32), numMappings (u32)
//   strings  numFilenames x { length (u16), bytes }
//   mappings numMappings x { spellId (u32), filenameIndex (u16) }, sorted by spellId
//
// Every count in the header is checked against the bytes actually present
// before anything is sized or read from it, so a damaged or hostile file is
// refused here rather than read past its end later.
namespace spell_icon_table {

constexpr uint32_t kMagic = 0x49434F4E;  // "ICON"
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr size_t kMappingSize = 6;

struct Layout {
    std::vector<std::string> filenames;
    uint32_t numMappings = 0;
    size_t mappingOffset = 0;
};

inline std::optional<Layout> parse(const char* data, size_t size, std::string& error) {
    if (size < kHeaderSize) {
        error = "Binary file too small: " + std::to_string(size) + " bytes";
        return std::nullopt;
    }

    uint32_t magic, version, numFilenames, numMappings;
    std::memcpy(&magic, data, 4);
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&numFilenames, data + 8, 4);
    std::memcpy(&numMappings, data + 12, 4);

    if (magic != kMagic) {
        error = "Invalid magic number: " + std::to_string(magic);
        return std::nullopt;
    }
    if (version != kVersion) {
        error = "Unsupported binary file version: " + std::to_string(version);
        return std::nullopt;
    }

    // Each filename takes at least its 2-byte length, which bounds the count
    if (numFilenames > (size - kHeaderSize) / 2) {
        error = "Filename count " + std::to_string(numFilenames) + " does not fit in the file";
        return std::nullopt;
    }

    Layout layout;
    layout.filenames.reserve(numFilenames);
    size_t offset = kHeaderSize;

    for (uint32_t i = 0; i < numFilenames; i++) {
        if (size - offset < 2) {
            error = "Truncated string table at index " + std::to_string(i);
            return std::nullopt;
        }
        uint16_t length;
        std::memcpy(&length, data + offset, 2);
        offset += 2;

        if (size - offset < length) {
            error = "Truncated filename at index " + std::to_string(i);
            return std::nullopt;
        }
        layout.filenames.emplace_back(data + offset, length);
        offset += length;
    }

    if (numMappings > (size - offset) / kMappingSize) {
        error = "Mapping count " + std::to_string(numMappings) + " does not fit in the file";
        return std::nullopt;
    }

    layout.numMappings = numMappings;
    layout.mappingOffset = offset;
    return layout;
}

} // namespace spell_icon_table
