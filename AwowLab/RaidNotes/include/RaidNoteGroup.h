#pragma once

#include "RaidNoteEntry.h"
#include <vector>
#include <string>

namespace RaidNotes {

enum class RaidNoteGroupType {
    BossCasts,      // Hostile creature abilities (boss mechanics)
    PlayerCasts,    // Friendly player abilities (cooldowns, defensives)
    Manual          // User-typed notes without spell association
};

// Entries of one type, exported under a shared header
struct RaidNoteGroup {
    RaidNoteGroupType type;
    std::string display_name;           // Header text (e.g., "-- Boss Abilities --")
    std::vector<RaidNoteEntry> entries;
    bool collapsed = false;             // UI state for collapsible section

    static RaidNoteGroup create(RaidNoteGroupType type);

    static std::string getDefaultDisplayName(RaidNoteGroupType type);

    void sortByTimestamp();

    RaidNoteEntry* findEntry(const std::string& entryId);
    const RaidNoteEntry* findEntry(const std::string& entryId) const;

    bool hasEnabledEntries() const;
};

} // namespace RaidNotes
