#pragma once

#include "RaidNoteDocument.h"
#include <string>

namespace RaidNotes {

struct ExportOptions {
    bool includeGroupHeaders = true;   // Add "-- Boss Abilities --" headers
    bool includeSpellNames = true;     // Include spell name in note text
    bool sortByTimestamp = true;       // Sort entries within groups by time
};

// Writes a RaidNoteDocument as an ERT (Exorsus Raid Tools) note using
// {time:M:SS} triggers, ready to paste in game
class RaidNoteExporter {
public:
    static std::string exportToERT(const RaidNoteDocument& doc, const ExportOptions& opts = {});

    // e.g. "{time:1:30}"
    static std::string formatTimestamp(int32_t ms);

    // One note line, without the newline
    static std::string formatEntry(const RaidNoteEntry& entry, bool includeSpellName);

private:
    static std::string formatGroupHeader(const RaidNoteGroup& group);
};

} // namespace RaidNotes
