#pragma once

#include "AutoAnnotationTypes.h"
#include <vector>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <cstdint>

class EventDatabase;
class DeathDatabase;
struct ActorEvent;

// One cached trigger hit. The position is captured at event time, so scrubbing
// the timeline does not move it.
struct CachedAnnotationEvent {
    uint64_t triggerId;
    int32_t timestamp_ms;         // When annotation should appear (event time - lead time)
    int32_t eventTimestamp_ms;    // actual event time, used for dedup
    float worldX, worldY;         // Target/anchor position captured at event time
    float sourceX, sourceY;       // Source/caster position (for Line shape)
    bool hasSourcePosition;
    std::string targetGuid;       // used for dedup

    // Position samples for During*/follow modes, lerped toward the next sample.
    int32_t sampleDurationMs = 0;   // usually 250ms
    int32_t debuffEndTime_ms = 0;
    bool isDuringDebuffSample = false;
    float nextX = 0.0f;
    float nextY = 0.0f;
    bool hasNextPosition = false;   // false on the last sample
    uint64_t sampleChainId = 0;     // shared by every sample of one aura instance, so one annotation is reused

    // CasterFacing/TargetFacing anchors
    uint16_t anchorFacing = 0;    // radians * 10000 (0-62831)
    bool hasFacing = false;

    // Actor triggers
    int32_t actorDeathTime_ms = INT32_MAX;  // INT32_MAX = did not die
    ActorDeathBehavior deathBehavior = ActorDeathBehavior::DisappearImmediately;
    bool isActorTrigger = false;

    // Projectile end point in world coordinates, for the ToTarget/InFacingDirection end modes.
    float projectileEndX = 0.0f;
    float projectileEndY = 0.0f;
    bool hasProjectileEnd = false;

    // Aura stacks at event time, used for stack colors.
    uint8_t stackCount = 1;

    // INT32_MAX = not negated
    int32_t negatedAt_ms = INT32_MAX;
};

struct TriggerEvaluatorContext {
    const std::vector<AutoAnnotationTrigger>& triggers;
    std::vector<CachedAnnotationEvent>& cachedEvents;
    std::unordered_set<uint64_t>& seenEvents;
};

namespace trigger_eval {

// Environment damage has an empty or all-zero source GUID.
inline bool isEnvironmentSource(const std::string& sourceGuid) {
    return sourceGuid.empty() ||
           sourceGuid == "0000000000000000" ||
           (sourceGuid.size() >= 6 && sourceGuid.starts_with("000000"));
}

// Dedup key used while building the cache.
uint64_t makeEventKey(uint64_t triggerId, int32_t timestamp_ms, const std::string& targetGuid);

bool findPositionAtTime(const EventDatabase* eventDb, const std::string& actorGuid,
                        int32_t timestamp_ms, float& outX, float& outY,
                        uint16_t* outFacing = nullptr);

// "Concentrated Void (1)" -> "Concentrated Void", so numbered instances match by name.
std::string stripInstanceSuffix(std::string_view name);

// Fade Over Time: how long an actor annotation takes to fade out once its actor dies.
constexpr int32_t kActorDeathFadeMs = 3000;

// The stretch over which an annotation fades after its actor's death.
// start == INT32_MAX means it doesn't fade.
struct DeathFade {
    int32_t start = INT32_MAX;
    int32_t end = INT32_MAX;
    bool fades() const { return start != INT32_MAX; }
};

// naturalHide is when the event would stop showing with no death involved.
// Follow samples stop at the death, so the last one is held for the fade;
// trail and spawn marks start fading at the death and never outlive their
// own duration.
inline DeathFade deathFadeFor(const CachedAnnotationEvent& event, int32_t naturalHide) {
    DeathFade fade;
    if (!event.isActorTrigger || event.deathBehavior != ActorDeathBehavior::FadeOverTime ||
        event.actorDeathTime_ms == INT32_MAX || event.timestamp_ms >= event.actorDeathTime_ms) {
        return fade;
    }
    if (event.isDuringDebuffSample) {
        if (event.eventTimestamp_ms + event.sampleDurationMs < event.actorDeathTime_ms) {
            return fade;
        }
        fade.start = naturalHide;
        fade.end = naturalHide + kActorDeathFadeMs;
        return fade;
    }
    if (naturalHide <= event.actorDeathTime_ms) {
        return fade;
    }
    fade.start = event.actorDeathTime_ms;
    fade.end = static_cast<int32_t>(
        std::min<int64_t>(naturalHide, int64_t{fade.start} + kActorDeathFadeMs));
    return fade;
}

// Opacity multiplier at the given time, 1 before the fade starts.
inline float deathFadeAlpha(const DeathFade& fade, int32_t time_ms) {
    if (!fade.fades() || time_ms <= fade.start) {
        return 1.0f;
    }
    float progress = static_cast<float>(time_ms - fade.start) / static_cast<float>(kActorDeathFadeMs);
    return progress >= 1.0f ? 0.0f : 1.0f - progress;
}

} // namespace trigger_eval
