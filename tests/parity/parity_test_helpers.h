#pragma once

#include "OutputWriter.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>

namespace parity {

struct ComparisonTolerances {
    int32_t timestamp_ms = 100;           // Timestamp matching tolerance
    double amount_percent = 0.01;         // 1% tolerance for amounts
    float position_units = 1.0f;          // Position tolerance in world units
};

struct ComparisonResult {
    bool matched = false;
    std::string difference;
};

struct ParityResult {
    bool passed = true;
    std::vector<std::string> differences;

    size_t combat_events_matched = 0;
    size_t combat_events_mismatched = 0;
    size_t aura_events_matched = 0;
    size_t aura_events_mismatched = 0;
    size_t cast_events_matched = 0;
    size_t cast_events_mismatched = 0;
    size_t death_events_matched = 0;
    size_t death_events_mismatched = 0;

    double getMatchRate() const {
        size_t total_matched = combat_events_matched + aura_events_matched +
                               cast_events_matched + death_events_matched;
        size_t total_mismatched = combat_events_mismatched + aura_events_mismatched +
                                  cast_events_mismatched + death_events_mismatched;
        size_t total = total_matched + total_mismatched;
        return total > 0 ? (double)total_matched / total : 0.0;
    }
};

// Maps local GUIDs to the synthetic GUIDs the WCL adapter generates.
class GuidNormalizer {
public:
    void addMapping(const std::string& local_guid, const std::string& wcl_guid);

    // Builds the synthetic GUID from wcl_id and the local GUID's type.
    void addMappingFromId(int32_t wcl_id, const std::string& local_guid);

    // True if both GUIDs refer to the same actor.
    bool areEquivalent(const std::string& guid1, const std::string& guid2) const;

    std::string toWclGuid(const std::string& local_guid) const;

    std::string toLocalGuid(const std::string& wcl_guid) const;

private:
    std::unordered_map<std::string, std::string> local_to_wcl_;
    std::unordered_map<std::string, std::string> wcl_to_local_;
};

inline bool withinTolerance(int64_t a, int64_t b, double percent) {
    if (a == b) return true;
    if (a == 0 || b == 0) return false;

    double diff = std::abs((double)(a - b));
    double larger = std::max(std::abs((double)a), std::abs((double)b));
    return (diff / larger) <= percent;
}

inline bool withinTolerance(int32_t a, int32_t b, int32_t absolute) {
    return std::abs(a - b) <= absolute;
}

inline bool withinTolerance(float a, float b, float absolute) {
    return std::abs(a - b) <= absolute;
}

// Compares time, spell, amount, event type and the crit and periodic flags;
// the actors are not compared.
ComparisonResult compareCombatEvents(
    const CombatEvent& local,
    const CombatEvent& wcl,
    const ComparisonTolerances& tol = {}
);

ComparisonResult compareAuraEvents(
    const AuraEvent& local,
    const AuraEvent& wcl,
    const ComparisonTolerances& tol = {}
);

ComparisonResult compareSpellCastEvents(
    const SpellCastEvent& local,
    const SpellCastEvent& wcl,
    const ComparisonTolerances& tol = {}
);

ComparisonResult compareDeathEvents(
    const DeathEvent& local,
    const DeathEvent& wcl,
    const ComparisonTolerances& tol = {}
);

// Pairs up local and WCL events and compares each pair.
ParityResult compareExtractedData(
    const ExtractedData& local,
    const ExtractedData& wcl,
    const GuidNormalizer& guidMap,
    const ComparisonTolerances& tol = {}
);

// Index into wcl, or SIZE_MAX if nothing matches.
size_t findMatchingCombatEvent(
    const CombatEvent& local,
    const std::vector<CombatEvent>& wcl_events,
    const std::vector<bool>& wcl_matched,
    const ComparisonTolerances& tol
);

size_t findMatchingAuraEvent(
    const AuraEvent& local,
    const std::vector<AuraEvent>& wcl_events,
    const std::vector<bool>& wcl_matched,
    const ComparisonTolerances& tol
);

// Reads actor_mapping from the fixtures metadata.json.
GuidNormalizer loadGuidMapping(const std::string& metadata_path);

} // namespace parity
