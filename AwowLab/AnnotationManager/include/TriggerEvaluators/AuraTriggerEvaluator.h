#pragma once

#include "TriggerEvaluators/TriggerEvaluatorCommon.h"

class AuraDatabase;
class EventDatabase;

namespace trigger_eval {

// OnDebuffApplied, DuringDebuff, OnDebuffEnd, Fixate and Tether triggers.
void buildDebuffCache(
    const TriggerEvaluatorContext& ctx,
    const AuraDatabase* auras,
    const EventDatabase* eventDb
);

// OnBuffApplied, DuringBuff and OnBuffEnd triggers, for buffs from hostile sources.
void buildBuffCache(
    const TriggerEvaluatorContext& ctx,
    const AuraDatabase* auras,
    const EventDatabase* eventDb
);

} // namespace trigger_eval
