// Position-aware loading for DeathDatabase: infers implicit resurrections
// from EventDatabase position data. Kept out of DeathDatabase.h so builds
// that never track positions (the live overlay) can use the two-argument
// loadEvents without pulling in EventDatabase.
#include "DeathDatabase.h"
#include "EventDatabase.h"

void DeathDatabase::loadEvents(const std::vector<DeathEvent>& deaths,
                               const std::vector<ResurrectEvent>& resurrects,
                               const EventDatabase* eventDb) {
    storeEvents(deaths, resurrects);

    // Infer implicit resurrections from position events
    // When players release (run back) in M+, there's no SPELL_RESURRECT event
    // If we see position data for a dead actor after their death, they're alive
    if (eventDb) {
        // storeEvents adds all deaths, then all resurrects. Inference reads
        // the event after each death, so put the timelines in time order
        // first or a second death never sees the release that follows it.
        sortAndIndex();
        inferResurrectionsFromPositionEvents(eventDb);
    }

    sortAndIndex();
}

void DeathDatabase::inferResurrectionsFromPositionEvents(const EventDatabase* eventDb) {
    if (!eventDb || eventDb->empty()) return;

    // Build a map of GUID -> first position event timestamp after each death
    // We iterate through all position events once and check against death times
    std::unordered_map<std::string, std::vector<int32_t>> guidToPositionTimestamps;

    // Collect all position timestamps per actor
    eventDb->forEachEvent([&guidToPositionTimestamps](const ActorEvent& event) {
        guidToPositionTimestamps[std::string(event.source_guid)].push_back(event.timestamp_ms);
    });

    // For each actor that died, check if they have position events after death
    for (auto& [guid, lifeEvents] : actorLifeEvents_) {
        auto posIt = guidToPositionTimestamps.find(guid);
        if (posIt == guidToPositionTimestamps.end()) continue;

        const auto& positionTimestamps = posIt->second;
        if (positionTimestamps.empty()) continue;

        // For each death event, check if there's a position event after it
        // that isn't already covered by an explicit resurrect
        for (size_t i = 0; i < lifeEvents.size(); ++i) {
            if (!lifeEvents[i].second) continue;  // Skip resurrect events

            int32_t deathTime = lifeEvents[i].first;

            // Check if there's already a resurrect after this death
            bool hasExplicitResurrect = false;
            int32_t nextEventTime = INT32_MAX;
            if (i + 1 < lifeEvents.size()) {
                nextEventTime = lifeEvents[i + 1].first;
                hasExplicitResurrect = !lifeEvents[i + 1].second;  // false = resurrect
            }

            if (hasExplicitResurrect) continue;  // Already have explicit resurrect

            // Find first position event after death (and before next death if any)
            // Use binary search since position timestamps are in order
            auto posUpper = std::upper_bound(positionTimestamps.begin(),
                                              positionTimestamps.end(),
                                              deathTime);

            if (posUpper != positionTimestamps.end()) {
                int32_t firstPosAfterDeath = *posUpper;

                // Only add implicit resurrect if position event is before next event
                if (firstPosAfterDeath < nextEventTime) {
                    // Add implicit resurrect at first position event after death
                    lifeEvents.emplace_back(firstPosAfterDeath, false);  // false = resurrect
                }
            }
        }
    }

    // The caller re-sorts the life events afterwards
}
