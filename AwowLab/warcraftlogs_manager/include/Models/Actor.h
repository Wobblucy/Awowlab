#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace warcraftlogs {

enum class ActorType {
    Unknown,
    Player,
    NPC,
    Pet,
    Boss,
    Guardian
};

inline ActorType parseActorType(std::string_view typeStr) noexcept {
    if (typeStr == "Player") return ActorType::Player;
    if (typeStr == "NPC") return ActorType::NPC;
    if (typeStr == "Pet") return ActorType::Pet;
    if (typeStr == "Boss") return ActorType::Boss;
    if (typeStr == "Guardian") return ActorType::Guardian;
    return ActorType::Unknown;
}

inline std::string_view actorTypeToString(ActorType type) noexcept {
    switch (type) {
        case ActorType::Player: return "Player";
        case ActorType::NPC: return "NPC";
        case ActorType::Pet: return "Pet";
        case ActorType::Boss: return "Boss";
        case ActorType::Guardian: return "Guardian";
        default: return "Unknown";
    }
}

// Represents an actor (player, NPC, pet, etc.) in a WarcraftLogs report.
struct Actor {
    int32_t id = 0;              // Report ID (unique within report, used in events)
    int64_t gameID = 0;          // WoW game ID (creature ID for NPCs, etc.)
    std::string name;            // Character/NPC name
    ActorType type = ActorType::Unknown;
    std::string subType;         // Class for players, NPC subtype
    int32_t petOwner = -1;       // Owner's report ID (-1 = no owner)
    std::string server;          // Normalized server name
    std::string icon;            // Icon for the actor

    [[nodiscard]] bool isPlayer() const noexcept {
        return type == ActorType::Player;
    }

    [[nodiscard]] bool isNPC() const noexcept {
        return type == ActorType::NPC || type == ActorType::Boss;
    }

    [[nodiscard]] bool isPet() const noexcept {
        return type == ActorType::Pet || type == ActorType::Guardian;
    }
};

} // namespace warcraftlogs
