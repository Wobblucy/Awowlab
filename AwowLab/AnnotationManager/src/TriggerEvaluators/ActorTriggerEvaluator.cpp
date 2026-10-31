#include "TriggerEvaluators/ActorTriggerEvaluator.h"
#include "Database/EventDatabase.h"
#include "Database/DeathDatabase.h"
#include <algorithm>
#include <unordered_set>

namespace trigger_eval {

std::vector<std::string> findActorsByName(
    const EventDatabase* eventDb,
    const std::string& actorName
) {
    if (!eventDb || actorName.empty()) return {};

    std::unordered_set<std::string> guids;
    eventDb->forEachEvent([&](const ActorEvent& event) {
        // "Concentrated Void (1)" should match a trigger for "Concentrated Void".
        std::string strippedName = stripInstanceSuffix(event.source_actor);
        if (strippedName == actorName) {
            guids.insert(std::string(event.source_guid));
        }
    });

    return {guids.begin(), guids.end()};
}

ActorLifetime getActorLifetime(
    const EventDatabase* eventDb,
    const DeathDatabase* deathDb,
    const std::string& actorGuid
) {
    ActorLifetime lifetime;
    std::string actorNameWithInstance;

    if (!eventDb) return lifetime;

    eventDb->forEachEvent([&](const ActorEvent& event) {
        if (event.source_guid != actorGuid) return;

        if (actorNameWithInstance.empty()) {
            actorNameWithInstance = std::string(event.source_actor);
        }

        if (event.timestamp_ms < lifetime.spawnTime_ms) {
            lifetime.spawnTime_ms = event.timestamp_ms;
        }
        if (event.timestamp_ms > lifetime.lastSeenTime_ms) {
            lifetime.lastSeenTime_ms = event.timestamp_ms;
        }
    });

    // Death time: match by GUID first, then exact name, then name without the
    // instance suffix, since the death record's suffix does not always agree.
    if (deathDb && !actorNameWithInstance.empty()) {
        const auto& allDeaths = deathDb->getAllDeaths();
        for (const auto& death : allDeaths) {
            if (death.actor_guid == actorGuid) {
                lifetime.deathTime_ms = death.timestamp_ms;
                break;
            }
        }

        if (lifetime.deathTime_ms == INT32_MAX) {
            for (const auto& death : allDeaths) {
                if (death.actor_name == actorNameWithInstance) {
                    lifetime.deathTime_ms = death.timestamp_ms;
                    break;
                }
            }
        }

        if (lifetime.deathTime_ms == INT32_MAX) {
            std::string strippedSearch = stripInstanceSuffix(actorNameWithInstance);
            for (const auto& death : allDeaths) {
                if (stripInstanceSuffix(death.actor_name) == strippedSearch) {
                    lifetime.deathTime_ms = death.timestamp_ms;
                    break;
                }
            }
        }
    }

    return lifetime;
}

void buildActorCache(
    const TriggerEvaluatorContext& ctx,
    const EventDatabase* eventDb,
    const DeathDatabase* deathDb
) {
    if (!eventDb) return;

    std::vector<const AutoAnnotationTrigger*> actorTriggers;
    for (const auto& trigger : ctx.triggers) {
        if (!trigger.enabled) continue;
        if (trigger.category != TriggerCategory::Actor) continue;
        actorTriggers.push_back(&trigger);
    }
    if (actorTriggers.empty()) return;

    for (const auto* trigger : actorTriggers) {
        std::vector<std::string> matchingActors = findActorsByName(eventDb, trigger->actorNameFilter);

        if (matchingActors.empty()) {
            continue;
        }

        for (const std::string& actorGuid : matchingActors) {
            ActorLifetime lifetime = getActorLifetime(eventDb, deathDb, actorGuid);

            if (lifetime.spawnTime_ms == INT32_MAX || lifetime.lastSeenTime_ms == INT32_MIN) {
                continue;
            }

            int32_t endTime = (lifetime.deathTime_ms != INT32_MAX)
                ? lifetime.deathTime_ms
                : lifetime.lastSeenTime_ms;

            switch (trigger->timing) {
                case TriggerTiming::ActorSpawn: {
                    int32_t eventTime = lifetime.spawnTime_ms;

                    uint64_t key = makeEventKey(trigger->id, eventTime, actorGuid);
                    if (ctx.seenEvents.count(key)) continue;
                    ctx.seenEvents.insert(key);

                    float x, y;
                    if (!findPositionAtTime(eventDb, actorGuid, eventTime, x, y)) {
                        continue;
                    }

                    x += trigger->offsetX;
                    y += trigger->offsetY;

                    CachedAnnotationEvent cached;
                    cached.triggerId = trigger->id;
                    cached.timestamp_ms = eventTime - trigger->leadTimeMs;
                    cached.eventTimestamp_ms = eventTime;
                    cached.worldX = x;
                    cached.worldY = y;
                    cached.targetGuid = actorGuid;
                    cached.hasSourcePosition = false;
                    cached.isActorTrigger = true;
                    cached.deathBehavior = trigger->deathBehavior;
                    cached.actorDeathTime_ms = lifetime.deathTime_ms;

                    ctx.cachedEvents.push_back(std::move(cached));
                    break;
                }

                case TriggerTiming::ActorDeath: {
                    if (lifetime.deathTime_ms == INT32_MAX) {
                        continue;
                    }

                    int32_t eventTime = lifetime.deathTime_ms;

                    uint64_t key = makeEventKey(trigger->id, eventTime, actorGuid);
                    if (ctx.seenEvents.count(key)) continue;
                    ctx.seenEvents.insert(key);

                    float x, y;
                    if (!findPositionAtTime(eventDb, actorGuid, eventTime, x, y)) {
                        continue;
                    }

                    x += trigger->offsetX;
                    y += trigger->offsetY;

                    CachedAnnotationEvent cached;
                    cached.triggerId = trigger->id;
                    cached.timestamp_ms = eventTime - trigger->leadTimeMs;
                    cached.eventTimestamp_ms = eventTime;
                    cached.worldX = x;
                    cached.worldY = y;
                    cached.targetGuid = actorGuid;
                    cached.hasSourcePosition = false;
                    cached.isActorTrigger = true;
                    cached.deathBehavior = trigger->deathBehavior;
                    cached.actorDeathTime_ms = lifetime.deathTime_ms;

                    ctx.cachedEvents.push_back(std::move(cached));
                    break;
                }

                case TriggerTiming::ActorFollow: {
                    // Position samples over the actor's whole lifetime, like DuringDebuff.
                    const int32_t sampleInterval = 250;
                    std::vector<CachedAnnotationEvent> samples;

                    for (int32_t t = lifetime.spawnTime_ms; t <= endTime; t += sampleInterval) {
                        uint64_t key = makeEventKey(trigger->id, t, actorGuid);
                        if (ctx.seenEvents.count(key)) continue;
                        ctx.seenEvents.insert(key);

                        float x, y;
                        if (!findPositionAtTime(eventDb, actorGuid, t, x, y)) {
                            continue;
                        }

                        x += trigger->offsetX;
                        y += trigger->offsetY;

                        CachedAnnotationEvent cached;
                        cached.triggerId = trigger->id;
                        cached.timestamp_ms = t - trigger->leadTimeMs;
                        cached.eventTimestamp_ms = t;
                        cached.worldX = x;
                        cached.worldY = y;
                        cached.targetGuid = actorGuid;
                        cached.hasSourcePosition = false;
                        cached.isActorTrigger = true;
                        cached.deathBehavior = trigger->deathBehavior;
                        cached.actorDeathTime_ms = lifetime.deathTime_ms;

                        // Follow samples reuse the DuringDebuff sample path so only one shows at a time.
                        cached.isDuringDebuffSample = true;
                        cached.sampleDurationMs = sampleInterval;
                        cached.debuffEndTime_ms = endTime;

                        samples.push_back(std::move(cached));
                    }

                    // Point each sample at the next for lerping, and stretch durations over
                    // gaps left by samples with no position.
                    for (size_t i = 0; i < samples.size(); ++i) {
                        if (i + 1 < samples.size()) {
                            samples[i].nextX = samples[i + 1].worldX;
                            samples[i].nextY = samples[i + 1].worldY;
                            samples[i].hasNextPosition = true;
                            samples[i].sampleDurationMs = samples[i + 1].eventTimestamp_ms - samples[i].eventTimestamp_ms;
                        } else {
                            samples[i].sampleDurationMs = samples[i].debuffEndTime_ms - samples[i].eventTimestamp_ms;
                        }
                        ctx.cachedEvents.push_back(std::move(samples[i]));
                    }
                    break;
                }

                case TriggerTiming::ActorTrail: {
                    const int32_t trailInterval = std::max(trigger->trailIntervalMs, kMinTrailIntervalMs);

                    for (int32_t t = lifetime.spawnTime_ms; t <= endTime; t += trailInterval) {
                        uint64_t key = makeEventKey(trigger->id, t, actorGuid);
                        if (ctx.seenEvents.count(key)) continue;
                        ctx.seenEvents.insert(key);

                        float x, y;
                        if (!findPositionAtTime(eventDb, actorGuid, t, x, y)) {
                            continue;
                        }

                        x += trigger->offsetX;
                        y += trigger->offsetY;

                        CachedAnnotationEvent cached;
                        cached.triggerId = trigger->id;
                        cached.timestamp_ms = t - trigger->leadTimeMs;
                        cached.eventTimestamp_ms = t;
                        cached.worldX = x;
                        cached.worldY = y;
                        cached.targetGuid = actorGuid;
                        cached.hasSourcePosition = false;
                        cached.isActorTrigger = true;
                        cached.deathBehavior = trigger->deathBehavior;
                        cached.actorDeathTime_ms = lifetime.deathTime_ms;

                        // Not a sample: trail marks last for the trigger's own duration.
                        ctx.cachedEvents.push_back(std::move(cached));
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
}

} // namespace trigger_eval
