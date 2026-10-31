#pragma once

#include "TriggerEvaluators/TriggerEvaluatorCommon.h"

class EventDatabase;
class DeathDatabase;

namespace trigger_eval {

struct ActorLifetime {
    int32_t spawnTime_ms = INT32_MAX;
    int32_t lastSeenTime_ms = INT32_MIN;
    int32_t deathTime_ms = INT32_MAX;  // only set when a DeathDatabase is given
};

// Unique actor GUIDs with this display name.
std::vector<std::string> findActorsByName(
    const EventDatabase* eventDb,
    const std::string& actorName
);

// First and last position timestamps, plus death time when known.
ActorLifetime getActorLifetime(
    const EventDatabase* eventDb,
    const DeathDatabase* deathDb,
    const std::string& actorGuid
);

// ActorSpawn, ActorDeath, ActorFollow and ActorTrail triggers.
void buildActorCache(
    const TriggerEvaluatorContext& ctx,
    const EventDatabase* eventDb,
    const DeathDatabase* deathDb
);

} // namespace trigger_eval
