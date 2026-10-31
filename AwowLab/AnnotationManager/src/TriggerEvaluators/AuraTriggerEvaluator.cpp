#include "TriggerEvaluators/AuraTriggerEvaluator.h"
#include "Database/AuraDatabase.h"
#include "Database/EventDatabase.h"
#include <algorithm>

namespace trigger_eval {

void buildDebuffCache(
    const TriggerEvaluatorContext& ctx,
    const AuraDatabase* auras,
    const EventDatabase* eventDb
) {
    if (!auras || !eventDb) return;

    std::vector<const AutoAnnotationTrigger*> debuffTriggers;
    for (const auto& trigger : ctx.triggers) {
        if (!trigger.enabled) continue;
        if (trigger.timing == TriggerTiming::OnDebuffApplied ||
            trigger.timing == TriggerTiming::DuringDebuff ||
            trigger.timing == TriggerTiming::OnDebuffEnd ||
            trigger.timing == TriggerTiming::Fixate ||
            trigger.timing == TriggerTiming::Tether) {
            debuffTriggers.push_back(&trigger);
        }
    }
    if (debuffTriggers.empty()) return;

    for (const auto& [targetGuid, instances] : auras->getAllTargetAuras()) {
        for (const auto& instance : instances) {
            if (instance.aura_type != AuraType::DEBUFF) continue;

            for (const auto* trigger : debuffTriggers) {
                if (instance.spell_id != trigger->spell_id) continue;

                if (trigger->sourceFilter == SourceFilter::EnvironmentOnly) {
                    if (!isEnvironmentSource(instance.source_guid)) continue;
                } else if (trigger->sourceFilter == SourceFilter::SpecificCreature) {
                    if (trigger->creature_guid.empty() || instance.source_guid != trigger->creature_guid) continue;
                }

                // Filter on max_stacks, so the condition reads "ever reached X stacks".
                uint8_t stackCount = instance.max_stacks > 0 ? instance.max_stacks : 1;
                if (!trigger->passesStackFilter(stackCount)) {
                    continue;
                }

                if (trigger->timing == TriggerTiming::OnDebuffApplied) {
                    int32_t eventTime = instance.applied_at_ms;

                    bool needsFacing = anchorUsesFacing(trigger->anchor);
                    bool anchorIsCaster = anchorAtCaster(trigger->anchor);
                    std::string anchorGuid = anchorIsCaster
                        ? instance.source_guid
                        : instance.target_guid;

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
                    cached.hasSourcePosition = false;
                    cached.stackCount = instance.getStackCountAt(eventTime);

                    if (needsFacing) {
                        cached.anchorFacing = facing;
                        cached.hasFacing = true;
                    }

                    bool needsSourcePosition = (trigger->shape == AnnotationShape::Line && !needsFacing) ||
                                               trigger->anchor == TriggerAnchor::SourceToTarget;
                    if (needsSourcePosition) {
                        float srcX, srcY;
                        std::string sourceGuid = (trigger->anchor == TriggerAnchor::Target ||
                                                  trigger->anchor == TriggerAnchor::SourceToTarget)
                            ? instance.source_guid
                            : instance.target_guid;
                        if (!sourceGuid.empty() && findPositionAtTime(eventDb, sourceGuid, eventTime, srcX, srcY)) {
                            cached.sourceX = srcX;
                            cached.sourceY = srcY;
                            cached.hasSourcePosition = true;
                        }
                    }

                    ctx.cachedEvents.push_back(std::move(cached));
                }
                else if (trigger->timing == TriggerTiming::DuringDebuff) {
                    // Samples the debuffed unit every 250ms. Only Target and TargetFacing apply.
                    std::string anchorGuid = instance.target_guid;
                    bool needsFacing = (trigger->anchor == TriggerAnchor::TargetFacing);

                    int32_t startTime = instance.applied_at_ms;
                    int32_t endTime = instance.removed_at_ms;

                    // Debuffs still up at the end of the log are capped at 60 seconds.
                    if (endTime == INT32_MAX) {
                        endTime = startTime + 60000;
                    }

                    const int32_t sampleInterval = 250;

                    // Every sample of this debuff instance shares one chain ID.
                    uint64_t chainId = makeEventKey(trigger->id, startTime, anchorGuid);

                    std::vector<CachedAnnotationEvent> samples;

                    for (int32_t t = startTime; t < endTime; t += sampleInterval) {
                        uint64_t key = makeEventKey(trigger->id, t, anchorGuid);
                        if (ctx.seenEvents.count(key)) continue;
                        ctx.seenEvents.insert(key);

                        float x, y;
                        uint16_t facing = 0;
                        if (!findPositionAtTime(eventDb, anchorGuid, t, x, y, needsFacing ? &facing : nullptr)) {
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
                        cached.targetGuid = anchorGuid;
                        cached.hasSourcePosition = false;
                        cached.stackCount = instance.getStackCountAt(t);

                        if (needsFacing) {
                            cached.anchorFacing = facing;
                            cached.hasFacing = true;
                        }

                        cached.isDuringDebuffSample = true;
                        cached.sampleDurationMs = std::min(sampleInterval, endTime - t);
                        cached.debuffEndTime_ms = endTime;
                        cached.sampleChainId = chainId;

                        samples.push_back(std::move(cached));
                    }

                    // Point each sample at the next for lerping, and stretch durations over
                    // gaps left by samples with no position. The last one runs to the debuff end.
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
                }
                else if (trigger->timing == TriggerTiming::Fixate) {
                    // Joins the source NPC to the target player, sampling both every 250ms.

                    const std::string& fixateSourceGuid = instance.source_guid;  // NPC
                    const std::string& fixateTargetGuid = instance.target_guid;  // Player

                    int32_t startTime = instance.applied_at_ms;
                    int32_t endTime = instance.removed_at_ms;

                    if (endTime == INT32_MAX) {
                        endTime = startTime + 60000;
                    }

                    uint64_t chainId = makeEventKey(trigger->id, startTime, fixateTargetGuid);

                    const int32_t sampleInterval = 250;
                    std::vector<CachedAnnotationEvent> samples;

                    for (int32_t t = startTime; t < endTime; t += sampleInterval) {
                        uint64_t key = makeEventKey(trigger->id, t, fixateTargetGuid);
                        if (ctx.seenEvents.count(key)) continue;
                        ctx.seenEvents.insert(key);

                        float tgtX, tgtY;
                        if (!findPositionAtTime(eventDb, fixateTargetGuid, t, tgtX, tgtY)) {
                            continue;
                        }

                        float srcX, srcY;
                        if (!findPositionAtTime(eventDb, fixateSourceGuid, t, srcX, srcY)) {
                            continue;
                        }

                        CachedAnnotationEvent cached;
                        cached.triggerId = trigger->id;
                        cached.timestamp_ms = t - trigger->leadTimeMs;
                        cached.eventTimestamp_ms = t;

                        cached.worldX = tgtX + trigger->offsetX;
                        cached.worldY = tgtY + trigger->offsetY;
                        cached.targetGuid = fixateTargetGuid;

                        cached.sourceX = srcX;
                        cached.sourceY = srcY;
                        cached.hasSourcePosition = true;

                        cached.stackCount = instance.getStackCountAt(t);
                        cached.isDuringDebuffSample = true;
                        cached.sampleDurationMs = std::min(sampleInterval, endTime - t);
                        cached.debuffEndTime_ms = endTime;
                        cached.sampleChainId = chainId;

                        samples.push_back(std::move(cached));
                    }

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
                }
                else if (trigger->timing == TriggerTiming::Tether) {
                    // Like Fixate, but the source end stays where the NPC was when the debuff landed.

                    const std::string& tgtGuid = instance.target_guid;
                    const std::string& srcGuid = instance.source_guid;

                    int32_t startTime = instance.applied_at_ms;
                    int32_t endTime = instance.removed_at_ms;

                    if (endTime == INT32_MAX) {
                        endTime = startTime + 60000;
                    }

                    float srcX, srcY;
                    if (!findPositionAtTime(eventDb, srcGuid, startTime, srcX, srcY)) {
                        continue;
                    }

                    uint64_t chainId = makeEventKey(trigger->id, startTime, tgtGuid);

                    const int32_t sampleInterval = 250;
                    std::vector<CachedAnnotationEvent> samples;

                    for (int32_t t = startTime; t < endTime; t += sampleInterval) {
                        uint64_t key = makeEventKey(trigger->id, t, tgtGuid);
                        if (ctx.seenEvents.count(key)) continue;
                        ctx.seenEvents.insert(key);

                        float tgtX, tgtY;
                        if (!findPositionAtTime(eventDb, tgtGuid, t, tgtX, tgtY)) {
                            continue;
                        }

                        CachedAnnotationEvent cached;
                        cached.triggerId = trigger->id;
                        cached.timestamp_ms = t - trigger->leadTimeMs;
                        cached.eventTimestamp_ms = t;

                        cached.worldX = tgtX + trigger->offsetX;
                        cached.worldY = tgtY + trigger->offsetY;
                        cached.targetGuid = tgtGuid;

                        cached.sourceX = srcX;
                        cached.sourceY = srcY;
                        cached.hasSourcePosition = true;

                        cached.stackCount = instance.getStackCountAt(t);
                        cached.isDuringDebuffSample = true;
                        cached.sampleDurationMs = std::min(sampleInterval, endTime - t);
                        cached.debuffEndTime_ms = endTime;
                        cached.sampleChainId = chainId;

                        samples.push_back(std::move(cached));
                    }

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
                }
                else if (trigger->timing == TriggerTiming::OnDebuffEnd) {
                    // Only Target and TargetFacing apply.
                    std::string anchorGuid = instance.target_guid;
                    bool needsFacing = (trigger->anchor == TriggerAnchor::TargetFacing);

                    int32_t eventTime = instance.removed_at_ms;

                    if (eventTime == INT32_MAX) {
                        continue;
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
                    cached.hasSourcePosition = false;
                    cached.stackCount = instance.getStackCountAt(eventTime);

                    if (needsFacing) {
                        cached.anchorFacing = facing;
                        cached.hasFacing = true;
                    }

                    ctx.cachedEvents.push_back(std::move(cached));
                }
            }
        }
    }
}

void buildBuffCache(
    const TriggerEvaluatorContext& ctx,
    const AuraDatabase* auras,
    const EventDatabase* eventDb
) {
    if (!auras || !eventDb) return;

    std::vector<const AutoAnnotationTrigger*> buffTriggers;
    for (const auto& trigger : ctx.triggers) {
        if (!trigger.enabled) continue;
        if (trigger.timing == TriggerTiming::OnBuffApplied ||
            trigger.timing == TriggerTiming::DuringBuff ||
            trigger.timing == TriggerTiming::OnBuffEnd) {
            buffTriggers.push_back(&trigger);
        }
    }
    if (buffTriggers.empty()) return;

    for (const auto& [targetGuid, instances] : auras->getAllTargetAuras()) {
        for (const auto& instance : instances) {
            if (instance.aura_type != AuraType::BUFF) continue;

            for (const auto* trigger : buffTriggers) {
                if (instance.spell_id != trigger->spell_id) continue;

                if (trigger->sourceFilter == SourceFilter::EnvironmentOnly) {
                    if (!isEnvironmentSource(instance.source_guid)) continue;
                } else if (trigger->sourceFilter == SourceFilter::SpecificCreature) {
                    if (trigger->creature_guid.empty() || instance.source_guid != trigger->creature_guid) continue;
                }

                // Filter on max_stacks, so the condition reads "ever reached X stacks".
                uint8_t stackCount = instance.max_stacks > 0 ? instance.max_stacks : 1;
                if (!trigger->passesStackFilter(stackCount)) {
                    continue;
                }

                // "Target" is the buffed unit, which for self-buffs is also the source.
                if (trigger->timing == TriggerTiming::OnBuffApplied) {
                    int32_t eventTime = instance.applied_at_ms;

                    bool needsFacing = anchorUsesFacing(trigger->anchor);
                    bool anchorIsCaster = anchorAtCaster(trigger->anchor);
                    std::string anchorGuid = anchorIsCaster
                        ? instance.source_guid
                        : instance.target_guid;

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
                    cached.hasSourcePosition = false;
                    cached.stackCount = instance.getStackCountAt(eventTime);

                    if (needsFacing) {
                        cached.anchorFacing = facing;
                        cached.hasFacing = true;
                    }

                    bool needsSourcePosition = (trigger->shape == AnnotationShape::Line && !needsFacing) ||
                                               trigger->anchor == TriggerAnchor::SourceToTarget;
                    if (needsSourcePosition) {
                        float srcX, srcY;
                        std::string sourceGuid = (trigger->anchor == TriggerAnchor::Target ||
                                                  trigger->anchor == TriggerAnchor::SourceToTarget)
                            ? instance.source_guid
                            : instance.target_guid;
                        if (!sourceGuid.empty() && findPositionAtTime(eventDb, sourceGuid, eventTime, srcX, srcY)) {
                            cached.sourceX = srcX;
                            cached.sourceY = srcY;
                            cached.hasSourcePosition = true;
                        }
                    }

                    ctx.cachedEvents.push_back(std::move(cached));
                }
                else if (trigger->timing == TriggerTiming::DuringBuff) {
                    // Samples the buffed unit every 250ms.
                    std::string anchorGuid = instance.target_guid;
                    bool needsFacing = (trigger->anchor == TriggerAnchor::CasterFacing ||
                                       trigger->anchor == TriggerAnchor::TargetFacing);

                    int32_t startTime = instance.applied_at_ms;
                    int32_t endTime = instance.removed_at_ms;

                    if (endTime == INT32_MAX) {
                        endTime = startTime + 60000;
                    }

                    uint64_t chainId = makeEventKey(trigger->id, startTime, anchorGuid);

                    const int32_t sampleInterval = 250;
                    std::vector<CachedAnnotationEvent> samples;

                    for (int32_t t = startTime; t < endTime; t += sampleInterval) {
                        uint64_t key = makeEventKey(trigger->id, t, anchorGuid);
                        if (ctx.seenEvents.count(key)) continue;
                        ctx.seenEvents.insert(key);

                        float x, y;
                        uint16_t facing = 0;
                        if (!findPositionAtTime(eventDb, anchorGuid, t, x, y, needsFacing ? &facing : nullptr)) {
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
                        cached.targetGuid = anchorGuid;
                        cached.hasSourcePosition = false;
                        cached.stackCount = instance.getStackCountAt(t);

                        if (needsFacing) {
                            cached.anchorFacing = facing;
                            cached.hasFacing = true;
                        }

                        // Buff samples reuse the DuringDebuff sample fields.
                        cached.isDuringDebuffSample = true;
                        cached.sampleDurationMs = std::min(sampleInterval, endTime - t);
                        cached.debuffEndTime_ms = endTime;
                        cached.sampleChainId = chainId;

                        samples.push_back(std::move(cached));
                    }

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
                }
                else if (trigger->timing == TriggerTiming::OnBuffEnd) {
                    std::string anchorGuid = instance.target_guid;
                    bool needsFacing = (trigger->anchor == TriggerAnchor::CasterFacing ||
                                       trigger->anchor == TriggerAnchor::TargetFacing);

                    int32_t eventTime = instance.removed_at_ms;

                    if (eventTime == INT32_MAX) {
                        continue;
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
                    cached.hasSourcePosition = false;
                    cached.stackCount = instance.getStackCountAt(eventTime);

                    if (needsFacing) {
                        cached.anchorFacing = facing;
                        cached.hasFacing = true;
                    }

                    ctx.cachedEvents.push_back(std::move(cached));
                }
            }
        }
    }
}

} // namespace trigger_eval
