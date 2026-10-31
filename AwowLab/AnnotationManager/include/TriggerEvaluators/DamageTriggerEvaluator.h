#pragma once

#include "TriggerEvaluators/TriggerEvaluatorCommon.h"

class CombatDatabase;
class EventDatabase;

namespace trigger_eval {

// OnDamage triggers.
void buildDamageCache(
    const TriggerEvaluatorContext& ctx,
    const CombatDatabase* combat,
    const EventDatabase* eventDb
);

} // namespace trigger_eval
