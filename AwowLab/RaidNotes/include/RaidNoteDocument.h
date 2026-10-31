#pragma once

#include "RaidNoteGroup.h"
#include <vector>
#include <string>
#include <optional>

namespace RaidNotes {

// Shifts every timestamp at or after after_ms by delay_ms
struct PhaseDelay {
    std::string id;
    int32_t after_ms = 0;
    int32_t delay_ms = 0;     // may be negative

    static std::string generateId();
};

class RaidNoteDocument {
public:
    RaidNoteDocument();

    // Entry management
    void addEntry(RaidNoteGroupType groupType, RaidNoteEntry entry);
    void removeEntry(const std::string& entryId);
    void updateEntry(const std::string& entryId, const RaidNoteEntry& updated);
    void toggleEntry(const std::string& entryId, bool enabled);

    // Phase delay management
    void addPhaseDelay(int32_t afterMs, int32_t delayMs);
    void removePhaseDelay(const std::string& delayId);
    void updatePhaseDelay(const std::string& delayId, int32_t afterMs, int32_t delayMs);
    const std::vector<PhaseDelay>& getPhaseDelays() const { return phaseDelays_; }
    std::vector<PhaseDelay>& getPhaseDelays() { return phaseDelays_; }

    // Timestamp after phase delays, used on export
    int32_t getAdjustedTimestamp(int32_t original_ms) const;

    // Bulk operations
    void clear();  // Remove all entries and phase delays

    // Query
    const std::vector<RaidNoteGroup>& getGroups() const { return groups_; }
    std::vector<RaidNoteGroup>& getGroups() { return groups_; }

    bool hasEntry(uint32_t spellId, int32_t timestamp_ms) const;
    bool isEmpty() const;
    size_t getEntryCount() const;

    // Searches every group
    RaidNoteEntry* findEntry(const std::string& entryId);
    const RaidNoteEntry* findEntry(const std::string& entryId) const;

    // Display only
    int32_t getGlobalOffset() const { return globalOffsetMs_; }
    void setGlobalOffset(int32_t offset_ms) { globalOffsetMs_ = offset_ms; }

    // Serialization for persistence
    std::string toJson() const;
    static std::optional<RaidNoteDocument> fromJson(const std::string& json);

private:
    std::vector<RaidNoteGroup> groups_;  // Always 3 groups: Boss, Player, Manual
    std::vector<PhaseDelay> phaseDelays_;
    int32_t globalOffsetMs_ = 0;         // cumulative offset applied

    RaidNoteGroup* getGroup(RaidNoteGroupType type);
    const RaidNoteGroup* getGroup(RaidNoteGroupType type) const;
};

} // namespace RaidNotes
