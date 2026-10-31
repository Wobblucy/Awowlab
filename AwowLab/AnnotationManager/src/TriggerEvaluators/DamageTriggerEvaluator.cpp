#include "TriggerEvaluators/DamageTriggerEvaluator.h"
#include "Database/CombatDatabase.h"
#include "Database/EventDatabase.h"
#include "Core/StringInterner.h"

namespace trigger_eval {

void buildDamageCache(
    const TriggerEvaluatorContext& ctx,
    const CombatDatabase* combat,
    const EventDatabase* eventDb
) {
    if (!combat || !eventDb) return;

    const auto* actorMap = combat->getActorMap();
    if (!actorMap) return;

    std::vector<const AutoAnnotationTrigger*> damageTriggers;
    for (const auto& trigger : ctx.triggers) {
        if (!trigger.enabled) continue;
        if (trigger.timing == TriggerTiming::OnDamage) {
            damageTriggers.push_back(&trigger);
        }
    }
    if (damageTriggers.empty()) return;

    for (const auto& [sourceGuidId, table] : *actorMap) {
        std::string_view sourceGuidSv = guidInterner().lookup(sourceGuidId);
        for (const auto& record : table.damage_dealt_table) {
            for (const auto* trigger : damageTriggers) {
                if (record.spell_id != trigger->spell_id) continue;

                if (!trigger->creature_guid.empty() && sourceGuidSv != trigger->creature_guid) {
                    continue;
                }

                int32_t eventTime = record.timestamp_ms;

                std::string_view targetGuidSv = guidInterner().lookup(record.target_guid_id);
                std::string targetGuid(targetGuidSv);
                std::string attackerGuid(sourceGuidSv);

                bool needsFacing = anchorUsesFacing(trigger->anchor);
                bool anchorIsCaster = anchorAtCaster(trigger->anchor);
                std::string anchorGuid = anchorIsCaster ? attackerGuid : targetGuid;

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

                // Lines and SourceToTarget join attacker and target.
                cached.hasSourcePosition = false;
                bool needsSourcePosition = (trigger->shape == AnnotationShape::Line && !needsFacing) ||
                                           trigger->anchor == TriggerAnchor::SourceToTarget;
                if (needsSourcePosition) {
                    float srcX, srcY;
                    // The source end is whichever of attacker and target the anchor is not on.
                    std::string lineSourceGuid = (trigger->anchor == TriggerAnchor::Target ||
                                                  trigger->anchor == TriggerAnchor::SourceToTarget)
                        ? attackerGuid
                        : targetGuid;
                    if (!lineSourceGuid.empty() && findPositionAtTime(eventDb, lineSourceGuid, eventTime, srcX, srcY)) {
                        cached.sourceX = srcX;
                        cached.sourceY = srcY;
                        cached.hasSourcePosition = true;
                    }
                }

                ctx.cachedEvents.push_back(std::move(cached));
            }
        }
    }
}

} // namespace trigger_eval
