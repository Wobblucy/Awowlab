#pragma once

#include "GuidGenerator.h"
#include "OutputWriter.h"  // ActorColor, held by value in an unordered_map
#include "Database/CooldownModifiers/CooldownModifierRegistry.h"
#include <rapidjson/document.h>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

struct ActorEvent;
struct ResourceEvent;
struct CombatEvent;
struct DeathEvent;
struct ResurrectEvent;
struct CombatantInfoEvent;
struct MapChangeEvent;
struct AuraEvent;
struct SpellCastEvent;
struct SummonEvent;
struct MissedEvent;
struct ExtractedData;
struct UnitFlags;
enum class ActorType;
enum class AuraEventType : uint8_t;

namespace warcraftlogs {

struct WclFight {
    int32_t id = 0;
    std::string name;
    uint64_t startTime = 0;       // Absolute timestamp in ms
    uint64_t endTime = 0;         // Absolute timestamp in ms
    int32_t encounterID = 0;
    int32_t difficulty = 0;
    int32_t size = 0;             // Raid size
    int32_t keystoneLevel = 0;    // M+ keystone level (0 if not M+)
    bool kill = false;            // Whether the pull killed the boss

    [[nodiscard]] uint64_t duration() const noexcept {
        return endTime > startTime ? endTime - startTime : 0;
    }

    [[nodiscard]] bool isMythicPlus() const noexcept {
        return keystoneLevel > 0;
    }
};

struct WclActor {
    int32_t id = 0;
    std::string name;
    std::string type;             // "Player", "NPC", "Pet", etc.
    WclActorType actorType = WclActorType::Unknown;
    std::string guid;             // Generated synthetic GUID
    int32_t petOwner = -1;        // Owner's report ID (-1 = no owner)
    // The real WoW creature ID. Synthetic GUIDs are built from WarcraftLogs'
    // per-report actor id, not this, so anything keyed on the real npc id must
    // use this field (see WclMetadata::guidToNpcId).
    uint32_t gameId = 0;

    // Class and specialisation, e.g. "DemonHunter" and "Devourer". Imported
    // reports carry no COMBATANT_INFO, so this is the only place a player's
    // spec appears - anything keyed on spec id has to resolve it from here.
    std::string className;        // subType, e.g. "DemonHunter"
    std::string specName;         // Parsed from icon, e.g. "Devourer"
    uint16_t specId = 0;          // 0 when the spec could not be resolved
};

struct WclReportMetadata {
    std::string reportCode;
    int64_t startTime = 0;    // Report start time (Unix epoch ms)
    std::vector<WclFight> fights;
    std::vector<WclActor> actors;
    std::unordered_map<int32_t, std::string> actorIdToName;
    std::unordered_map<int32_t, std::string> actorIdToGuid;
    // WarcraftLogs actor id -> real WoW creature id. Synthetic GUIDs encode the
    // WCL actor id (plus a per-spawn suffix), not the creature id, so anything
    // keyed on npc id resolves through this table.
    std::unordered_map<int32_t, uint32_t> actorIdToNpcId;
};

// Reports `current` of `total` items converted so far.
using ConversionProgressCallback = std::function<void(size_t current, size_t total, const std::string& message)>;

// Converts WarcraftLogs JSON (report metadata and event pages) into the
// ExtractedData the local log parser produces. Actors get synthetic GUIDs,
// timestamps become relative to the fight start, and coordinates are
// divided by 100 into world units.
class WclToAwowAdapter {
public:
    WclToAwowAdapter();
    ~WclToAwowAdapter();

    // Maps a WCL hitType to the miss it stands for, or Unknown when the attack
    // landed. WCL has no miss event (a dodge is a damage event with no amount),
    // so this is the only source for defense.csv. Public so the tests can check
    // it against the cached reports.
    static MissType missTypeFromHitType(int32_t hitType);

    WclReportMetadata parseReportMetadata(const rapidjson::Document& metadataJson);

    // Throws std::runtime_error if the string isn't valid JSON.
    WclReportMetadata parseReportMetadata(const std::string& jsonString);

    // fightStartTime is the fight's absolute start, so event times come out
    // relative to it.
    ExtractedData convertEvents(
        const rapidjson::Document& eventsJson,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ConversionProgressCallback progressCallback = nullptr
    );

    ExtractedData convertEvents(
        const std::string& jsonString,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ConversionProgressCallback progressCallback = nullptr
    );

    // Combines the pages of one fight, as the API returns them.
    static ExtractedData mergePages(const std::vector<ExtractedData>& pages);

    [[nodiscard]] const ActorIdMapper& getActorMapper() const noexcept { return m_actorMapper; }

    // Call before the first page of each fight.
    void resetFightState() {
        m_actorResourceModifiers.clear();
        m_pendingCasts.clear();
        m_petToOwnerMap.clear();
        m_currentMapId = 0;
    }

private:
    void processEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    void processDamageEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    void processHealEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    void processDeathEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    void processAuraEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output,
        AuraEventType eventType
    );

    void processSummonEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    void processResurrectEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    // WCL's equivalent of SPELL_ENERGIZE. Carries the amount gained and, in
    // "waste", the part that spilled over the cap - the same measurement local
    // logs put in over_energize, and the only direct read on overcapping.
    void processResourceChangeEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    void processCombatantInfoEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        ExtractedData& output
    );

    void processCastEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    // The cooldown time a cast buys back by spending a resource, for actors
    // whose talents tie a cooldown to that resource.
    void addResourceCooldownReductions(
        const rapidjson::Value& event,
        const SpellCastEvent& cast,
        int32_t timestamp,
        ExtractedData& output
    );

    void processAbsorbedEvent(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    // False when the event carries no position.
    bool extractPositionData(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    // Health and power, when the event carries them.
    void extractResourceData(
        const rapidjson::Value& event,
        const WclReportMetadata& metadata,
        uint64_t fightStartTime,
        ExtractedData& output
    );

    std::string getActorGuid(int32_t actorId, const WclReportMetadata& metadata) const;
    std::string getActorGuidWithInstance(int32_t actorId, int32_t instanceId, const WclReportMetadata& metadata) const;
    std::string getActorName(int32_t actorId, const WclReportMetadata& metadata) const;
    std::string getActorNameWithInstance(int32_t actorId, int32_t instanceId, const WclReportMetadata& metadata) const;
    ActorType convertActorType(WclActorType wclType) const;

    // WarcraftLogs positions are world coordinates times 100.
    static float convertCoordinate(int64_t wclCoord);

    // Fight-relative milliseconds; negative for events before the pull.
    static int32_t convertTimestamp(uint64_t wclTimestamp, uint64_t fightStartTime);

    ActorIdMapper m_actorMapper;

    // (actorId, spellId) -> begincast timestamp, for pairing a begincast with
    // the cast that completes it.
    struct PairHash {
        std::size_t operator()(const std::pair<int32_t, uint32_t>& p) const {
            return std::hash<int64_t>{}(
                (static_cast<int64_t>(p.first) << 32) | static_cast<int64_t>(p.second));
        }
    };
    std::unordered_map<std::pair<int32_t, uint32_t>, int32_t, PairHash> m_pendingCasts;

    // petId -> ownerId, from summon events and sourceOwner, so pet damage is
    // credited to the owner.
    std::unordered_map<int32_t, int32_t> m_petToOwnerMap;

    // For detecting map changes.
    uint16_t m_currentMapId = 0;

    // Resource-spend cooldown modifiers per actor, grouped by power type.
    struct ActorResourceModifiers {
        std::unordered_map<int8_t, std::vector<awow::CooldownModifier>> modifiers_by_power_type;
    };
    std::unordered_map<std::string, ActorResourceModifiers> m_actorResourceModifiers;
};

} // namespace warcraftlogs
