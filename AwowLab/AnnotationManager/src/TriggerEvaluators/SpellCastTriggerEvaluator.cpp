#include "TriggerEvaluators/SpellCastTriggerEvaluator.h"
#include "Database/SpellCastDatabase.h"
#include "Database/EventDatabase.h"
#include <cmath>

namespace trigger_eval {

void buildCastCache(
    const TriggerEvaluatorContext& ctx,
    const SpellCastDatabase* spellCasts,
    const EventDatabase* eventDb
) {
    if (!spellCasts || !eventDb) {
        return;
    }

    std::vector<const AutoAnnotationTrigger*> castTriggers;
    std::vector<const AutoAnnotationTrigger*> castStartTriggers;
    std::vector<const AutoAnnotationTrigger*> duringCastTriggers;
    for (const auto& trigger : ctx.triggers) {
        if (!trigger.enabled) continue;
        if (trigger.timing == TriggerTiming::OnCastSuccess) {
            castTriggers.push_back(&trigger);
        } else if (trigger.timing == TriggerTiming::OnCastStart) {
            castStartTriggers.push_back(&trigger);
        } else if (trigger.timing == TriggerTiming::DuringCast) {
            duringCastTriggers.push_back(&trigger);
        }
    }
    if (castTriggers.empty() && castStartTriggers.empty() && duringCastTriggers.empty()) return;

    for (const auto& [actorGuid, casts] : spellCasts->getAllActorCasts()) {
        for (const auto& cast : casts) {
            for (const auto* trigger : castTriggers) {
                if (cast.spell_id != trigger->spell_id) continue;

                if (!trigger->creature_guid.empty() && actorGuid != trigger->creature_guid) {
                    continue;
                }

                int32_t eventTime = cast.end_timestamp_ms;

                // Filters the fake 0:00 boss casts some logs start with.
                if (trigger->ignoreEarlyPullCasts && eventTime < 500) {
                    continue;
                }

                std::string anchorGuid;
                bool needsFacing = anchorUsesFacing(trigger->anchor);
                bool anchorIsCaster = anchorAtCaster(trigger->anchor);

                if (anchorIsCaster) {
                    anchorGuid = actorGuid;
                } else {
                    bool hasValidTarget = !cast.target_guid.empty() &&
                                          cast.target_guid != actorGuid &&
                                          !cast.target_guid.starts_with("000000");
                    if (hasValidTarget) {
                        anchorGuid = cast.target_guid;
                    } else {
                        // Spells aimed at the ground or environment fall back to the caster.
                        anchorGuid = actorGuid;
                    }
                }

                uint64_t key = makeEventKey(trigger->id, eventTime, anchorGuid);
                if (ctx.seenEvents.count(key)) continue;
                ctx.seenEvents.insert(key);

                float x, y;
                uint16_t facing = 0;
                if (!findPositionAtTime(eventDb, anchorGuid, eventTime, x, y, needsFacing ? &facing : nullptr)) {
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
                cached.targetGuid = anchorGuid;

                if (needsFacing) {
                    cached.anchorFacing = facing;
                    cached.hasFacing = true;
                }

                // Lines with a Caster/Target anchor, and any shape with SourceToTarget, join
                // caster and target. Facing anchors extend along the facing and need no source.
                cached.hasSourcePosition = false;
                bool needsSourcePosition = (trigger->shape == AnnotationShape::Line && !needsFacing) ||
                                           trigger->anchor == TriggerAnchor::SourceToTarget;
                if (needsSourcePosition) {
                    float srcX, srcY;
                    // SourceToTarget always sources from the caster; a Line sources from
                    // whichever end the anchor is not on.
                    std::string sourceGuid = (trigger->anchor == TriggerAnchor::Target ||
                                              trigger->anchor == TriggerAnchor::SourceToTarget) ? actorGuid : cast.target_guid;
                    if (!sourceGuid.empty() && findPositionAtTime(eventDb, sourceGuid, eventTime, srcX, srcY)) {
                        cached.sourceX = srcX;
                        cached.sourceY = srcY;
                        cached.hasSourcePosition = true;
                    }
                }

                if (trigger->motion.type == AnnotationMotionType::Projectile) {
                    switch (trigger->motion.endMode) {
                        case ProjectileEndMode::Manual:
                            // AnnotationMotionCalculator uses endOffsetX/Y directly.
                            break;

                        case ProjectileEndMode::ToTarget: {
                            // Anchored on the target, the projectile flies back toward the caster.
                            std::string targetForProjectile = anchorIsCaster ? cast.target_guid : actorGuid;
                            if (!targetForProjectile.empty() && !targetForProjectile.starts_with("000000")) {
                                float targetX, targetY;
                                if (findPositionAtTime(eventDb, targetForProjectile, eventTime, targetX, targetY)) {
                                    cached.projectileEndX = targetX;
                                    cached.projectileEndY = targetY;
                                    cached.hasProjectileEnd = true;
                                }
                            }
                            // Without a target the manual offsets are used at runtime.
                            break;
                        }

                        case ProjectileEndMode::InFacingDirection: {
                            // Always the caster's facing, whatever the anchor.
                            uint16_t casterFacing = 0;
                            float casterX, casterY;
                            if (findPositionAtTime(eventDb, actorGuid, eventTime, casterX, casterY, &casterFacing)) {
                                float facingRad = static_cast<float>(casterFacing) / 10000.0f;
                                constexpr float degToRad = 3.14159265358979f / 180.0f;
                                facingRad += trigger->motion.facingOffset_deg * degToRad;
                                float distance = trigger->motion.projectileDistance;
                                cached.projectileEndX = cached.worldX + distance * std::cos(facingRad);
                                cached.projectileEndY = cached.worldY + distance * std::sin(facingRad);
                                cached.hasProjectileEnd = true;
                            }
                            // Without facing data the manual offsets are used at runtime.
                            break;
                        }
                    }
                }

                ctx.cachedEvents.push_back(std::move(cached));
            }

            for (const auto* trigger : castStartTriggers) {
                if (cast.spell_id != trigger->spell_id) continue;

                // Instant casts have no cast bar and so no start.
                if (cast.start_timestamp_ms == 0) continue;

                if (!trigger->creature_guid.empty() && actorGuid != trigger->creature_guid) {
                    continue;
                }

                int32_t eventTime = cast.start_timestamp_ms;

                if (trigger->ignoreEarlyPullCasts && eventTime < 500) {
                    continue;
                }

                // Cast-start triggers always anchor on the caster; facing anchors use the caster's facing.
                std::string anchorGuid = actorGuid;
                bool needsFacing = anchorUsesFacing(trigger->anchor);

                uint64_t key = makeEventKey(trigger->id, eventTime, anchorGuid);
                if (ctx.seenEvents.count(key)) continue;
                ctx.seenEvents.insert(key);

                float x, y;
                uint16_t facing = 0;
                if (!findPositionAtTime(eventDb, anchorGuid, eventTime, x, y, needsFacing ? &facing : nullptr)) {
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
                cached.targetGuid = anchorGuid;

                if (needsFacing) {
                    cached.anchorFacing = facing;
                    cached.hasFacing = true;
                }

                cached.hasSourcePosition = false;
                bool needsSourcePosition = (trigger->shape == AnnotationShape::Line && !needsFacing) ||
                                           trigger->anchor == TriggerAnchor::SourceToTarget;
                if (needsSourcePosition && !cast.target_guid.empty()) {
                    float tgtX, tgtY;
                    if (findPositionAtTime(eventDb, cast.target_guid, eventTime, tgtX, tgtY)) {
                        // Runs from caster (source) to the cast target (world position).
                        cached.sourceX = x;
                        cached.sourceY = y;
                        cached.worldX = tgtX;
                        cached.worldY = tgtY;
                        cached.hasSourcePosition = true;
                    }
                }

                ctx.cachedEvents.push_back(std::move(cached));
            }

            // Drops a pool at the caster every interval while the cast runs.
            for (const auto* trigger : duringCastTriggers) {
                if (cast.spell_id != trigger->spell_id) continue;

                if (!trigger->creature_guid.empty() && actorGuid != trigger->creature_guid) {
                    continue;
                }

                if (trigger->ignoreEarlyPullCasts && cast.end_timestamp_ms < 500) {
                    continue;
                }

                int32_t startTime = cast.start_timestamp_ms - trigger->leadTimeMs;
                int32_t endTime = cast.end_timestamp_ms + trigger->castExtendMs;
                const int32_t interval = std::max(trigger->trailIntervalMs, kMinTrailIntervalMs);

                for (int32_t t = startTime; t <= endTime; t += interval) {
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
                    cached.timestamp_ms = t;
                    cached.eventTimestamp_ms = t;
                    cached.worldX = x;
                    cached.worldY = y;
                    cached.targetGuid = actorGuid;
                    cached.hasSourcePosition = false;

                    ctx.cachedEvents.push_back(std::move(cached));
                }
            }
        }
    }
}

} // namespace trigger_eval
