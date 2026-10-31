#pragma once

#include "TriggerEvaluators/TriggerEvaluatorCommon.h"

class CombatDatabase;
class EventDatabase;

namespace trigger_eval {

// OnDamageTaken triggers. Only Player- GUIDs count as victims; pets, guardians
// and NPCs are skipped.
void buildDamageTakenCache(
    const TriggerEvaluatorContext& ctx,
    const CombatDatabase* combat,
    const EventDatabase* eventDb
);

} // namespace trigger_eval
