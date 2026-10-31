#pragma once

#include "TriggerEvaluators/TriggerEvaluatorCommon.h"

class SpellCastDatabase;
class EventDatabase;

namespace trigger_eval {

// OnCastSuccess, OnCastStart and DuringCast triggers.
void buildCastCache(
    const TriggerEvaluatorContext& ctx,
    const SpellCastDatabase* spellCasts,
    const EventDatabase* eventDb
);

} // namespace trigger_eval
