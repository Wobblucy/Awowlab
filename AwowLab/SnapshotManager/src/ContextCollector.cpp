#include "ContextCollector.h"
#include <algorithm>
#include <unordered_set>

namespace snapshot {

SnapshotContext ContextCollector::collect(
    const ExtractedData& data,
    const std::vector<Annotation>& annotations,
    const std::vector<AutoAnnotationTrigger>& triggers,
    const std::vector<CachedAnnotationEvent>& cachedEvents,
    int32_t windowStartMs,
    int32_t windowEndMs,
    StringTableBuilder& strings
) {
    SnapshotContext context;

    context.activeAuras = findActiveAuras(data.auraEvents, windowStartMs, strings);

    context.annotations = filterAnnotations(annotations, windowStartMs, windowEndMs);

    // Triggers are rules and cached events carry their own times, so both are copied whole
    context.triggers = triggers;

    context.cachedEvents = cachedEvents;

    context.lastPositions = findLastPositions(data.actorEvents, windowStartMs, strings);

    return context;
}

std::vector<AuraSnapshot> ContextCollector::findActiveAuras(
    const std::vector<AuraEvent>& auraEvents,
    int32_t windowStartMs,
    StringTableBuilder& strings
) {
    struct AuraKey {
        std::string target_guid;
        uint32_t spell_id;

        bool operator==(const AuraKey& other) const {
            return target_guid == other.target_guid && spell_id == other.spell_id;
        }
    };

    struct AuraKeyHash {
        size_t operator()(const AuraKey& k) const {
            return std::hash<std::string>{}(k.target_guid) ^
                   (std::hash<uint32_t>{}(k.spell_id) << 1);
        }
    };

    struct ActiveAuraInfo {
        std::string source_guid;
        int32_t applied_at_ms;
        uint8_t stacks;
        AuraType aura_type;
    };

    std::unordered_map<AuraKey, ActiveAuraInfo, AuraKeyHash> activeAuras;

    auto cutoff = std::lower_bound(
        auraEvents.begin(), auraEvents.end(), windowStartMs,
        [](const AuraEvent& e, int32_t ts) { return e.timestamp_ms < ts; }
    );

    for (auto it = auraEvents.begin(); it != cutoff; ++it) {
        const auto& event = *it;
        AuraKey key{std::string(event.target_guid), event.spell_id};

        switch (event.event_type) {
            case AuraEventType::Applied:
            case AuraEventType::AppliedDose:
                activeAuras[key] = ActiveAuraInfo{
                    std::string(event.source_guid),
                    event.timestamp_ms,
                    event.stacks,
                    event.aura_type
                };
                break;

            case AuraEventType::Refresh:
                // Keep the original apply time
                if (auto auraIt = activeAuras.find(key); auraIt != activeAuras.end()) {
                    auraIt->second.stacks = event.stacks;
                }
                break;

            case AuraEventType::RemovedDose:
                if (auto auraIt = activeAuras.find(key); auraIt != activeAuras.end()) {
                    auraIt->second.stacks = event.stacks;
                }
                break;

            case AuraEventType::Removed:
            case AuraEventType::Broken:
            case AuraEventType::BrokenSpell:
                activeAuras.erase(key);
                break;

            default:
                break;
        }
    }

    std::vector<AuraSnapshot> result;
    result.reserve(activeAuras.size());

    for (const auto& [key, info] : activeAuras) {
        AuraSnapshot snapshot{};
        snapshot.target_guid_id = strings.intern(key.target_guid);
        snapshot.source_guid_id = strings.intern(info.source_guid);
        snapshot.spell_id = key.spell_id;
        snapshot.applied_at_ms = info.applied_at_ms;
        snapshot.remaining_ms = -1;  // unknown
        snapshot.stacks = info.stacks;
        snapshot.aura_type = static_cast<uint8_t>(info.aura_type);
        result.push_back(snapshot);
    }

    return result;
}

std::vector<Annotation> ContextCollector::filterAnnotations(
    const std::vector<Annotation>& annotations,
    int32_t /*windowStartMs*/,
    int32_t /*windowEndMs*/
) {
    std::vector<Annotation> result;
    result.reserve(annotations.size());

    for (const auto& ann : annotations) {
        // Visibility is decided at runtime by Annotation::isVisibleAtTime()
        if (!ann.deleted) {
            result.push_back(ann);
        }
    }

    return result;
}

std::vector<PositionSnapshot> ContextCollector::findLastPositions(
    const std::vector<ActorEvent>& actorEvents,
    int32_t windowStartMs,
    StringTableBuilder& strings
) {
    std::unordered_map<std::string, PositionSnapshot> lastPositions;

    auto cutoff = std::lower_bound(
        actorEvents.begin(), actorEvents.end(), windowStartMs,
        [](const ActorEvent& e, int32_t ts) { return e.timestamp_ms < ts; }
    );

    for (auto it = actorEvents.begin(); it != cutoff; ++it) {
        const auto& event = *it;
        std::string guid(event.source_guid);
        PositionSnapshot& snapshot = lastPositions[guid];
        snapshot.guid_id = strings.intern(guid);
        snapshot.name_id = strings.intern(event.source_actor);
        snapshot.x = event.x_loc;
        snapshot.y = event.y_loc;
        snapshot.facing = event.facing_radians;
        snapshot.actor_type = static_cast<uint8_t>(event.actor_type);
        snapshot.flags = packUnitFlags(
            static_cast<uint8_t>(event.flags.affiliation),
            static_cast<uint8_t>(event.flags.reaction),
            static_cast<uint8_t>(event.flags.controller),
            static_cast<uint8_t>(event.flags.unitType)
        );
    }

    std::vector<PositionSnapshot> result;
    result.reserve(lastPositions.size());
    for (auto& [guid, snapshot] : lastPositions) {
        result.push_back(std::move(snapshot));
    }

    return result;
}

} // namespace snapshot
