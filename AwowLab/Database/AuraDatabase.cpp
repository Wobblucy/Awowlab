#include "AuraDatabase.h"
#include <algorithm>
#include <tuple>

// Open aura instances are keyed by (target_guid, spell_id). The source is
// left out because the applying and removing source can differ, and a
// removal with no matching apply still has to find the instance.
struct AuraKey {
    std::string target_guid;
    uint32_t spell_id;

    bool operator==(const AuraKey& other) const {
        return target_guid == other.target_guid &&
               spell_id == other.spell_id;
    }
};

struct AuraKeyHash {
    size_t operator()(const AuraKey& key) const {
        size_t h1 = std::hash<std::string>{}(key.target_guid);
        size_t h2 = std::hash<uint32_t>{}(key.spell_id);
        return h1 ^ (h2 << 1);
    }
};

void AuraDatabase::loadFromEvents(
    std::vector<AuraEvent>&& events,
    const std::unordered_map<std::string, int32_t>& firstSeenTimestamps
) {
    clear();

    if (events.empty()) {
        return;
    }

    std::sort(events.begin(), events.end(),
        [](const AuraEvent& a, const AuraEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    extractCCBreaks(events);

    buildAuraInstances(events, firstSeenTimestamps);
    buildSourceIndex();
}

void AuraDatabase::clear() {
    targetAuras_.clear();
    sourceIndex_.clear();
    ccBreaks_.clear();
    min_timestamp_ = INT32_MAX;
    max_timestamp_ = 0;
}

// The returned pointer is valid only until targetAuras[target_guid] next grows
static AuraInstance* getOpenInstance(
    std::unordered_map<std::string, std::vector<AuraInstance>>& targetAuras,
    std::unordered_map<AuraKey, size_t, AuraKeyHash>& openInstanceIndices,
    const AuraKey& key
) {
    auto it = openInstanceIndices.find(key);
    if (it == openInstanceIndices.end()) {
        return nullptr;
    }
    auto targetIt = targetAuras.find(key.target_guid);
    if (targetIt == targetAuras.end() || it->second >= targetIt->second.size()) {
        return nullptr;
    }
    return &targetIt->second[it->second];
}

void AuraDatabase::buildAuraInstances(
    std::vector<AuraEvent>& events,
    const std::unordered_map<std::string, int32_t>& firstSeenTimestamps
) {
    // Indices rather than pointers, because the per-target vectors reallocate
    std::unordered_map<AuraKey, size_t, AuraKeyHash> openInstanceIndices;

    for (const auto& event : events) {
        if (event.timestamp_ms < min_timestamp_) {
            min_timestamp_ = event.timestamp_ms;
        }
        if (event.timestamp_ms > max_timestamp_) {
            max_timestamp_ = event.timestamp_ms;
        }

        AuraKey key{event.target_guid, event.spell_id};

        switch (event.event_type) {
            case AuraEventType::Applied: {
                if (AuraInstance* existing = getOpenInstance(targetAuras_, openInstanceIndices, key)) {
                    // Some log sources send repeated applies for stacks instead of
                    // apply + applystack. An apply within 100ms of the open one is
                    // taken as another stack.
                    if (event.timestamp_ms == existing->applied_at_ms ||
                        (event.timestamp_ms - existing->applied_at_ms) <= 100) {
                        existing->current_stacks++;
                        if (existing->current_stacks > existing->max_stacks) {
                            existing->max_stacks = existing->current_stacks;
                        }
                        existing->doseHistory.push_back({event.timestamp_ms, existing->current_stacks});
                        break;
                    }
                    existing->removed_at_ms = event.timestamp_ms;
                    openInstanceIndices.erase(key);
                }

                AuraInstance instance;
                instance.target_guid = event.target_guid;
                instance.source_guid = event.source_guid;
                instance.spell_id = event.spell_id;
                instance.spell_name = event.spell_name;
                instance.aura_type = event.aura_type;
                instance.applied_at_ms = event.timestamp_ms;
                instance.removed_at_ms = INT32_MAX;  // Still active
                // Use event stacks when the log provides them, default to 1 otherwise
                uint8_t initialStacks = event.stacks > 0 ? event.stacks : 1;
                instance.current_stacks = initialStacks;
                instance.max_stacks = initialStacks;

                instance.doseHistory.push_back({event.timestamp_ms, initialStacks});

                auto& targetList = targetAuras_[event.target_guid];
                size_t newIndex = targetList.size();
                targetList.push_back(std::move(instance));

                // Track as open instance by index
                openInstanceIndices[key] = newIndex;
                break;
            }

            case AuraEventType::Removed:
            case AuraEventType::Broken:
            case AuraEventType::BrokenSpell: {
                // Close the open instance
                if (AuraInstance* existing = getOpenInstance(targetAuras_, openInstanceIndices, key)) {
                    existing->removed_at_ms = event.timestamp_ms;
                    openInstanceIndices.erase(key);
                } else {
                    // A removal with nothing open: the aura went up before the log
                    // window (or from an unnamed source), so backdate it to when the
                    // target was first seen.
                    int32_t appliedTime = event.timestamp_ms;  // Default: same as removal
                    auto it = firstSeenTimestamps.find(event.target_guid);
                    if (it != firstSeenTimestamps.end()) {
                        appliedTime = it->second;
                    }

                    AuraInstance instance;
                    instance.target_guid = event.target_guid;
                    instance.source_guid = event.source_guid;  // Keep source for display
                    instance.spell_id = event.spell_id;
                    instance.spell_name = event.spell_name;
                    instance.aura_type = event.aura_type;
                    instance.applied_at_ms = appliedTime;      // Backdated to first-seen
                    instance.removed_at_ms = event.timestamp_ms;
                    // Use stack count from REMOVED event (default to 1 if not provided)
                    uint8_t stacks = event.stacks > 0 ? event.stacks : 1;
                    instance.current_stacks = stacks;
                    instance.max_stacks = stacks;
                    instance.start_inferred = true;            // Mark as synthetic

                    // Initialize dose history at inferred start with actual stack count
                    instance.doseHistory.push_back({appliedTime, stacks});

                    auto& targetList = targetAuras_[event.target_guid];
                    targetList.push_back(std::move(instance));
                    // Already closed, so not added to openInstanceIndices
                }
                break;
            }

            case AuraEventType::Refresh: {
                // An open instance already runs to INT32_MAX, so a refresh only
                // matters when nothing is open yet.
                if (!getOpenInstance(targetAuras_, openInstanceIndices, key)) {
                    // Aura was applied before encounter start, create instance
                    AuraInstance instance;
                    instance.target_guid = event.target_guid;
                    instance.source_guid = event.source_guid;
                    instance.spell_id = event.spell_id;
                    instance.spell_name = event.spell_name;
                    instance.aura_type = event.aura_type;
                    instance.applied_at_ms = event.timestamp_ms;  // Use refresh time as start
                    instance.removed_at_ms = INT32_MAX;
                    // Use event stacks if provided, default to 1
                    uint8_t initialStacks = event.stacks > 0 ? event.stacks : 1;
                    instance.current_stacks = initialStacks;
                    instance.max_stacks = initialStacks;

                    instance.doseHistory.push_back({event.timestamp_ms, initialStacks});

                    auto& targetList = targetAuras_[event.target_guid];
                    size_t newIndex = targetList.size();
                    targetList.push_back(std::move(instance));
                    openInstanceIndices[key] = newIndex;
                }
                break;
            }

            case AuraEventType::AppliedDose: {
                // Stack added - update stack counts
                if (AuraInstance* existing = getOpenInstance(targetAuras_, openInstanceIndices, key)) {
                    existing->current_stacks = event.stacks;
                    if (event.stacks > existing->max_stacks) {
                        existing->max_stacks = event.stacks;
                    }
                    // Record dose change in history
                    existing->doseHistory.push_back({event.timestamp_ms, event.stacks});
                } else {
                    // Aura was applied before encounter start, create instance
                    AuraInstance instance;
                    instance.target_guid = event.target_guid;
                    instance.source_guid = event.source_guid;
                    instance.spell_id = event.spell_id;
                    instance.spell_name = event.spell_name;
                    instance.aura_type = event.aura_type;
                    instance.applied_at_ms = event.timestamp_ms;
                    instance.removed_at_ms = INT32_MAX;
                    instance.current_stacks = event.stacks;
                    instance.max_stacks = event.stacks;

                    instance.doseHistory.push_back({event.timestamp_ms, event.stacks});

                    auto& targetList = targetAuras_[event.target_guid];
                    size_t newIndex = targetList.size();
                    targetList.push_back(std::move(instance));
                    openInstanceIndices[key] = newIndex;
                }
                break;
            }

            case AuraEventType::RemovedDose: {
                // Losing a stack does not end the aura
                if (AuraInstance* existing = getOpenInstance(targetAuras_, openInstanceIndices, key)) {
                    existing->current_stacks = event.stacks;
                    // Record dose change in history
                    existing->doseHistory.push_back({event.timestamp_ms, event.stacks});
                }
                // If no open instance, we can't do much - aura was applied before start
                break;
            }
        }
    }

    // Sort each target's aura list by applied_at_ms for efficient querying
    for (auto& [guid, auras] : targetAuras_) {
        std::sort(auras.begin(), auras.end(),
            [](const AuraInstance& a, const AuraInstance& b) {
                return a.applied_at_ms < b.applied_at_ms;
            });
    }
}

void AuraDatabase::buildSourceIndex() {
    sourceIndex_.clear();

    for (auto& [targetGuid, auras] : targetAuras_) {
        for (auto& instance : auras) {
            sourceIndex_[instance.source_guid].push_back(&instance);
        }
    }

    // Sort each source's list by applied_at_ms
    for (auto& [sourceGuid, ptrs] : sourceIndex_) {
        std::sort(ptrs.begin(), ptrs.end(),
            [](const AuraInstance* a, const AuraInstance* b) {
                return a->applied_at_ms < b->applied_at_ms;
            });
    }
}

std::vector<const AuraInstance*> AuraDatabase::getActiveAurasOnTarget(
    std::string_view target_guid,
    int32_t timestamp_ms
) const {
    std::vector<const AuraInstance*> result;

    auto it = targetAuras_.find(std::string(target_guid));
    if (it == targetAuras_.end()) {
        return result;
    }

    for (const auto& instance : it->second) {
        if (instance.isActiveAt(timestamp_ms)) {
            result.push_back(&instance);
        }
    }

    return result;
}

std::vector<const AuraInstance*> AuraDatabase::getActiveAurasFromSource(
    const std::string& source_guid,
    int32_t timestamp_ms
) const {
    std::vector<const AuraInstance*> result;

    auto it = sourceIndex_.find(source_guid);
    if (it == sourceIndex_.end()) {
        return result;
    }

    for (const auto* instance : it->second) {
        if (instance->isActiveAt(timestamp_ms)) {
            result.push_back(instance);
        }
    }

    return result;
}

const AuraInstance* AuraDatabase::getAuraInstance(
    const std::string& target_guid,
    const std::string& source_guid,
    uint32_t spell_id,
    int32_t timestamp_ms
) const {
    auto it = targetAuras_.find(target_guid);
    if (it == targetAuras_.end()) {
        return nullptr;
    }

    // Search for matching instance that was active at the given time
    // Return the most recent one if multiple match
    const AuraInstance* best = nullptr;

    for (const auto& instance : it->second) {
        if (instance.source_guid == source_guid &&
            instance.spell_id == spell_id &&
            instance.isActiveAt(timestamp_ms)) {
            // Keep the most recently applied matching instance
            if (!best || instance.applied_at_ms > best->applied_at_ms) {
                best = &instance;
            }
        }
    }

    return best;
}

std::vector<const AuraInstance*> AuraDatabase::getAllAurasOnTarget(
    const std::string& target_guid
) const {
    std::vector<const AuraInstance*> result;

    auto it = targetAuras_.find(target_guid);
    if (it == targetAuras_.end()) {
        return result;
    }

    result.reserve(it->second.size());
    for (const auto& instance : it->second) {
        result.push_back(&instance);
    }

    return result;
}

std::vector<const AuraInstance*> AuraDatabase::getAllAurasFromSource(
    const std::string& source_guid
) const {
    std::vector<const AuraInstance*> result;

    auto it = sourceIndex_.find(source_guid);
    if (it == sourceIndex_.end()) {
        return result;
    }

    result.reserve(it->second.size());
    for (const auto* instance : it->second) {
        result.push_back(instance);
    }

    return result;
}

uint8_t AuraDatabase::getStackCount(
    const std::string& target_guid,
    const std::string& source_guid,
    uint32_t spell_id,
    int32_t timestamp_ms
) const {
    const AuraInstance* instance = getAuraInstance(target_guid, source_guid, spell_id, timestamp_ms);
    if (!instance) {
        return 0;
    }
    // Use time-accurate stack count from dose history
    return instance->getStackCountAt(timestamp_ms);
}

void AuraDatabase::extractCCBreaks(const std::vector<AuraEvent>& events) {
    ccBreaks_.clear();

    for (const auto& event : events) {
        if (event.event_type == AuraEventType::Broken ||
            event.event_type == AuraEventType::BrokenSpell) {
            CCBreakEvent ccBreak;
            ccBreak.breaker_guid = event.source_guid;  // Who broke the CC
            ccBreak.target_guid = event.target_guid;   // Who had the CC
            ccBreak.cc_spell_id = event.spell_id;
            ccBreak.cc_spell_name = event.spell_name;
            ccBreak.breaking_spell_id = event.breaking_spell_id;
            ccBreak.breaking_spell_name = event.breaking_spell_name;
            ccBreak.timestamp_ms = event.timestamp_ms;

            ccBreaks_.push_back(std::move(ccBreak));
        }
    }

    std::sort(ccBreaks_.begin(), ccBreaks_.end(),
        [](const CCBreakEvent& a, const CCBreakEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });
}

size_t AuraDatabase::getCCBreakCount(int32_t start_time_ms, int32_t end_time_ms) const {
    size_t count = 0;
    for (const auto& ccBreak : ccBreaks_) {
        if (ccBreak.timestamp_ms >= start_time_ms && ccBreak.timestamp_ms <= end_time_ms) {
            ++count;
        }
    }
    return count;
}

std::vector<ActorCCBreakStats> AuraDatabase::getRankedByCCBreaks(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    // Aggregate breaks by breaker
    std::unordered_map<std::string, ActorCCBreakStats> statsMap;
    uint32_t totalBreaks = 0;

    for (const auto& ccBreak : ccBreaks_) {
        if (ccBreak.timestamp_ms < start_time_ms || ccBreak.timestamp_ms > end_time_ms) {
            continue;
        }

        auto& stats = statsMap[ccBreak.breaker_guid];
        stats.actor_guid = ccBreak.breaker_guid;
        stats.break_count++;
        totalBreaks++;

        // Track breakdown by CC spell
        bool found = false;
        for (auto& detail : stats.breakdown) {
            if (detail.cc_spell_id == ccBreak.cc_spell_id) {
                detail.count++;
                found = true;
                break;
            }
        }
        if (!found) {
            stats.breakdown.push_back({ccBreak.cc_spell_id, ccBreak.cc_spell_name, 1});
        }
    }

    std::vector<ActorCCBreakStats> result;
    result.reserve(statsMap.size());

    for (auto& [guid, stats] : statsMap) {
        if (totalBreaks > 0) {
            stats.percent_of_total = (static_cast<float>(stats.break_count) / totalBreaks) * 100.0f;
        }
        result.push_back(std::move(stats));
    }

    std::sort(result.begin(), result.end(),
        [](const ActorCCBreakStats& a, const ActorCCBreakStats& b) {
            return a.break_count > b.break_count;
        });

    // Limit results
    if (result.size() > max_results) {
        result.resize(max_results);
    }

    return result;
}
