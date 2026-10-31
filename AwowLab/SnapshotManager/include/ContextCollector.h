#pragma once
#include "SnapshotFormat.h"
#include "StringTableBuilder.h"
#include "Database/EventTypes.h"
#include "AnnotationManager/include/AnnotationTypes.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "AnnotationManager/include/TriggerEvaluators/TriggerEvaluatorCommon.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace snapshot {

// State from before the export window that a replay of the window needs
struct SnapshotContext {
    std::vector<AuraSnapshot> activeAuras;           // Auras active at window_start
    std::vector<Annotation> annotations;              // All non-deleted manual annotations
    std::vector<AutoAnnotationTrigger> triggers;      // Auto-annotation rules
    std::vector<CachedAnnotationEvent> cachedEvents;  // Pre-computed trigger cache
    std::vector<PositionSnapshot> lastPositions;      // Last known positions before window_start
};

// Works out the state at the start of an export window from the full
// encounter, so an imported snapshot doesn't start with missing auras or
// actors.
class ContextCollector {
public:
    ContextCollector() = default;

    // Window times are ms relative to encounter start. GUIDs are interned into strings.
    SnapshotContext collect(
        const ExtractedData& data,
        const std::vector<Annotation>& annotations,
        const std::vector<AutoAnnotationTrigger>& triggers,
        const std::vector<CachedAnnotationEvent>& cachedEvents,
        int32_t windowStartMs,
        int32_t windowEndMs,
        StringTableBuilder& strings
    );

private:
    // Replays apply/remove events up to windowStartMs (auraEvents must be sorted)
    std::vector<AuraSnapshot> findActiveAuras(
        const std::vector<AuraEvent>& auraEvents,
        int32_t windowStartMs,
        StringTableBuilder& strings
    );

    // Drops deleted annotations only; the window bounds are currently unused
    // because the viewer checks visibility at runtime
    std::vector<Annotation> filterAnnotations(
        const std::vector<Annotation>& annotations,
        int32_t windowStartMs,
        int32_t windowEndMs
    );

    // Last position of each actor before windowStartMs (actorEvents must be sorted)
    std::vector<PositionSnapshot> findLastPositions(
        const std::vector<ActorEvent>& actorEvents,
        int32_t windowStartMs,
        StringTableBuilder& strings
    );
};

} // namespace snapshot
