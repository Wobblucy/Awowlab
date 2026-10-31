#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>

namespace warcraftlogs {

// Actor types as WarcraftLogs names them.
enum class WclActorType {
    Unknown = 0,
    Player = 1,
    NPC = 2,
    Pet = 3,
    Guardian = 4,
    Boss = 5
};

[[nodiscard]] WclActorType parseActorType(const std::string& typeStr) noexcept;

[[nodiscard]] const char* getGuidPrefix(WclActorType type) noexcept;

// WarcraftLogs identifies actors by small per-report integers (105, 117),
// while the rest of AwowLab keys everything on combat-log GUIDs. These
// synthetic GUIDs, {Type}-0-0-0-0-{ActorID}, let WCL actors use the same
// structures:
//   Player 105  -> "Player-0-0-0-0-105"
//   NPC 12345   -> "Creature-0-0-0-0-12345"
//   Pet 200     -> "Pet-0-0-0-0-200"
class GuidGenerator {
public:
    [[nodiscard]] static std::string generate(int32_t actorId, WclActorType actorType);
    [[nodiscard]] static std::string generate(int32_t actorId, const std::string& actorTypeStr);

    // False if guid is not a synthetic GUID.
    [[nodiscard]] static bool parse(
        const std::string& guid,
        int32_t& outActorId,
        WclActorType& outActorType
    ) noexcept;

    [[nodiscard]] static bool isSyntheticGuid(const std::string& guid) noexcept;

    // -1 if guid is not a synthetic GUID.
    [[nodiscard]] static int32_t extractActorId(const std::string& guid) noexcept;
};

// Two-way lookup between WarcraftLogs actor ids and their synthetic GUIDs.
class ActorIdMapper {
public:
    // Returns the actor's GUID, creating it on first registration.
    std::string registerActor(int32_t actorId, WclActorType actorType, const std::string& actorName);

    // Empty if the actor is not registered.
    [[nodiscard]] std::string getGuid(int32_t actorId) const noexcept;

    // -1 if the GUID is not registered.
    [[nodiscard]] int32_t getActorId(const std::string& guid) const noexcept;

    // Empty if the actor is not registered.
    [[nodiscard]] std::string getActorName(int32_t actorId) const noexcept;

    // Unknown if the actor is not registered.
    [[nodiscard]] WclActorType getActorType(int32_t actorId) const noexcept;

    void clear() noexcept;

    [[nodiscard]] size_t size() const noexcept;

private:
    struct ActorInfo {
        std::string guid;
        std::string name;
        WclActorType type;
    };

    std::unordered_map<int32_t, ActorInfo> m_idToInfo;
    std::unordered_map<std::string, int32_t> m_guidToId;
};

} // namespace warcraftlogs
