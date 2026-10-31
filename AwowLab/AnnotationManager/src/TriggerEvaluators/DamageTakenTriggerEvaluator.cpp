#include "TriggerEvaluators/DamageTakenTriggerEvaluator.h"
#include "Database/CombatDatabase.h"
#include "Database/EventDatabase.h"
#include "Core/StringInterner.h"

namespace trigger_eval {

void buildDamageTakenCache(
    const TriggerEvaluatorContext& ctx,
    const CombatDatabase* combat,
    const EventDatabase* eventDb
) {
    if (!combat || !eventDb) return;

    // Keyed by victim GUID.
    const auto& damageTakenIndex = combat->getDamageTakenIndex();
    if (damageTakenIndex.empty()) return;

    std::vector<const AutoAnnotationTrigger*> damageTakenTriggers;
    for (const auto& trigger : ctx.triggers) {
        if (!trigger.enabled) continue;
        if (trigger.timing == TriggerTiming::OnDamageTaken) {
            damageTakenTriggers.push_back(&trigger);
        }
    }
    if (damageTakenTriggers.empty()) return;

    for (const auto& [targetGuidId, recordPtrs] : damageTakenIndex) {
        std::string_view victimGuidSv = guidInterner().lookup(targetGuidId);

        if (!victimGuidSv.starts_with("Player-")) continue;

        std::string victimGuid(victimGuidSv);

        for (const CombatRecord* record : recordPtrs) {
            for (const auto* trigger : damageTakenTriggers) {
                if (record->spell_id != trigger->spell_id) continue;

                int32_t eventTime = record->timestamp_ms;

                uint64_t key = makeEventKey(trigger->id, eventTime, victimGuid);
                if (ctx.seenEvents.count(key)) continue;
                ctx.seenEvents.insert(key);

                float x, y;
                uint16_t facing = 0;
                bool needsFacing = anchorUsesFacing(trigger->anchor);
                if (!findPositionAtTime(eventDb, victimGuid, eventTime, x, y,
                                        needsFacing ? &facing : nullptr)) {
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
                cached.targetGuid = victimGuid;

                if (needsFacing) {
                    cached.anchorFacing = facing;
                    cached.hasFacing = true;
                }

                // The damage-taken index has no attacker GUID, so lines cannot join
                // attacker and victim; they extend along the facing instead.
                cached.hasSourcePosition = false;

                ctx.cachedEvents.push_back(std::move(cached));
            }
        }
    }
}

} // namespace trigger_eval
