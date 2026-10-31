#include "TriggerEvaluators/TriggerEvaluatorCommon.h"
#include "Database/EventDatabase.h"
#include <functional>

namespace trigger_eval {

uint64_t makeEventKey(uint64_t triggerId, int32_t timestamp_ms, const std::string& targetGuid) {
    // The full GUID keeps separate instances of the same creature apart.
    uint64_t hash = triggerId;
    hash ^= static_cast<uint64_t>(timestamp_ms) << 16;
    hash ^= std::hash<std::string>{}(targetGuid);
    return hash;
}

bool findPositionAtTime(const EventDatabase* eventDb, const std::string& actorGuid,
                        int32_t timestamp_ms, float& outX, float& outY,
                        uint16_t* outFacing) {
    if (!eventDb) return false;

    // Latest position at or before the timestamp, within 5s. If there is none before,
    // fall back to the earliest one after.
    float bestX = 0, bestY = 0;
    uint16_t bestFacing = 0;
    int32_t bestTime = INT32_MIN;
    bool found = false;

    eventDb->forEachEventInWindow(timestamp_ms, 5000, [&](const ActorEvent& event) {
        if (event.source_guid != actorGuid) return;

        if (event.timestamp_ms <= timestamp_ms) {
            if (event.timestamp_ms > bestTime) {
                bestTime = event.timestamp_ms;
                bestX = event.x_loc;
                bestY = event.y_loc;
                bestFacing = event.facing_radians;
                found = true;
            }
        } else if (!found) {
            if (bestTime == INT32_MIN || event.timestamp_ms < bestTime) {
                bestTime = event.timestamp_ms;
                bestX = event.x_loc;
                bestY = event.y_loc;
                bestFacing = event.facing_radians;
                found = true;
            }
        }
    });

    if (found) {
        outX = bestX;
        outY = bestY;
        if (outFacing) {
            *outFacing = bestFacing;
        }
    }
    return found;
}

std::string stripInstanceSuffix(std::string_view name) {
    if (name.size() < 4) return std::string(name);

    size_t parenClose = name.rfind(')');
    if (parenClose != name.size() - 1) return std::string(name);

    size_t parenOpen = name.rfind('(');
    if (parenOpen == std::string_view::npos || parenOpen < 2) return std::string(name);

    if (name[parenOpen - 1] != ' ') return std::string(name);

    std::string_view between = name.substr(parenOpen + 1, parenClose - parenOpen - 1);
    if (between.empty()) return std::string(name);
    for (char c : between) {
        if (c < '0' || c > '9') return std::string(name);
    }

    return std::string(name.substr(0, parenOpen - 1));
}

} // namespace trigger_eval
