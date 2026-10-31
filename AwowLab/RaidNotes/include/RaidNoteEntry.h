#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace RaidNotes {

// One spell cast at a time, or a free-text note
struct RaidNoteEntry {
    std::string id;
    uint32_t spell_id = 0;       // 0 for manual entries
    std::string spell_name;
    int32_t timestamp_ms = 0;    // relative to encounter start
    std::string custom_text;     // e.g. "Defensive" or a player name
    bool enabled = true;         // disabled entries are kept but not exported

    // An "all instances" entry expands to one line per timestamp on export
    bool is_template = false;
    std::vector<int32_t> instance_timestamps;

    static std::string generateId();

    bool isManual() const { return spell_id == 0; }

    // instance_timestamps for a template, otherwise just timestamp_ms
    std::vector<int32_t> getEffectiveTimestamps() const;
};

} // namespace RaidNotes
