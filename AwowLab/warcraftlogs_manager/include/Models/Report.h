#pragma once

#include "Fight.h"
#include "Actor.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace warcraftlogs {

struct Report {
    std::string code;                                    // Report code (e.g., "fZB1pak4ycMhFYPT")
    int64_t startTime = 0;                               // Report start time (Unix epoch ms)
    std::vector<Fight> fights;                           // All fights in the report
    std::vector<Actor> actors;                           // All actors (players, NPCs, pets)
    std::unordered_map<int32_t, std::string> actorMap;   // Actor ID -> name mapping

    // "Unknown(id)" when the id is not in the report.
    [[nodiscard]] std::string getActorName(int32_t actorId) const {
        auto it = actorMap.find(actorId);
        if (it != actorMap.end()) {
            return it->second;
        }
        return "Unknown(" + std::to_string(actorId) + ")";
    }

    [[nodiscard]] std::optional<Actor> findActor(int32_t actorId) const {
        for (const auto& actor : actors) {
            if (actor.id == actorId) {
                return actor;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Fight> findFight(int32_t fightId) const {
        for (const auto& fight : fights) {
            if (fight.id == fightId) {
                return fight;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Fight> getValidFights() const {
        std::vector<Fight> result;
        for (const auto& fight : fights) {
            if (fight.isValid()) {
                result.push_back(fight);
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<Fight> getBossEncounters() const {
        std::vector<Fight> result;
        for (const auto& fight : fights) {
            if (fight.isBossEncounter() && fight.isValid()) {
                result.push_back(fight);
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<Actor> getPlayers() const {
        std::vector<Actor> result;
        for (const auto& actor : actors) {
            if (actor.isPlayer()) {
                result.push_back(actor);
            }
        }
        return result;
    }

    // Fills actorMap from actors; call once actors is populated.
    void buildActorMap() {
        actorMap.clear();
        for (const auto& actor : actors) {
            actorMap[actor.id] = actor.name;
        }
    }
};

} // namespace warcraftlogs
