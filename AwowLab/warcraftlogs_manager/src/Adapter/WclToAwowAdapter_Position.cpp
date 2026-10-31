// Position and health/power data carried on WarcraftLogs events.

#include "Adapter/WclToAwowAdapter.h"
#include "Adapter/WclJsonHelpers.h"
#include "OutputWriter.h"
#include "Core/StringInterner.h"
#include <cmath>

namespace warcraftlogs {

using namespace json;

namespace {

// WarcraftLogs stores positions multiplied by 100.
constexpr float COORD_SCALE = 100.0f;

// WarcraftLogs stores facing as (radians - 2.5π) * 100, so every value is
// negative. convertFacing adds the offset back, mirrors the angle
// (2π - angle), normalises it to [0, 2π) and returns radians * 10000, the
// format local logs are stored in.
constexpr float TWO_PI = 6.28318530718f;
constexpr float TWO_AND_HALF_PI = 7.85398163397f;

uint16_t convertFacing(int32_t wclFacing) {
    float radians = static_cast<float>(wclFacing) / 100.0f + TWO_AND_HALF_PI;

    radians = TWO_PI - radians;

    radians = std::fmod(radians, TWO_PI);
    if (radians < 0) radians += TWO_PI;

    return static_cast<uint16_t>(std::lround(radians * 10000.0f));
}

} // anonymous namespace

bool WclToAwowAdapter::extractPositionData(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    if (!event.HasMember("x") || !event.HasMember("y")) {
        return false;
    }

    // resourceActor says whose position this is (1 source, 2 target). Without
    // it, prefer the target.
    int32_t actorId = -1;
    int32_t instanceId = 0;
    int resourceActor = getInt(event, "resourceActor", 0);

    if (resourceActor == 1) {
        actorId = getInt(event, "sourceID");
        instanceId = getInt(event, "sourceInstance", 0);
    } else if (resourceActor == 2) {
        actorId = getInt(event, "targetID");
        instanceId = getInt(event, "targetInstance", 0);
    } else {
        if (event.HasMember("targetID")) {
            actorId = getInt(event, "targetID");
            instanceId = getInt(event, "targetInstance", 0);
        } else {
            actorId = getInt(event, "sourceID");
            instanceId = getInt(event, "sourceInstance", 0);
        }
    }

    if (actorId < 0) {
        return false;
    }

    ActorEvent actorEvent;
    // The instance number goes into the GUID so mobs sharing an id stay apart.
    // Both strings are interned so the string_views outlive the temporaries.
    std::string generatedGuid = getActorGuidWithInstance(actorId, instanceId, metadata);
    std::string generatedName = getActorNameWithInstance(actorId, instanceId, metadata);

    actorEvent.source_guid = guidInterner().lookup(guidInterner().intern(generatedGuid));
    actorEvent.source_actor = actorNameInterner().lookup(actorNameInterner().intern(generatedName));
    actorEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);

    // WCL's axes are swapped relative to the game's: our x is WCL y and our y
    // is -(WCL x).
    actorEvent.x_loc = convertCoordinate(getInt64(event, "y"));
    actorEvent.y_loc = -convertCoordinate(getInt64(event, "x"));
    uint16_t mapId = static_cast<uint16_t>(getInt(event, "mapID"));
    actorEvent.map_id = mapId;

    // Stored as radians * 10000, like the local parser.
    int32_t wclFacing = getInt(event, "facing", 0);
    actorEvent.facing_radians = convertFacing(wclFacing);

    if (mapId != 0 && mapId != m_currentMapId) {
        m_currentMapId = mapId;

        MapChangeEvent mapEvent;
        mapEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
        mapEvent.map_id = mapId;
        mapEvent.map_name = "";
        // Bounds come from master_map_alignment_coordinates.json in the UI.
        mapEvent.x1 = 0.0f;
        mapEvent.y1 = 0.0f;
        mapEvent.x2 = 0.0f;
        mapEvent.y2 = 0.0f;

        output.mapChangeEvents.push_back(std::move(mapEvent));
    }

    WclActorType wclType = m_actorMapper.getActorType(actorId);
    actorEvent.actor_type = convertActorType(wclType);

    UnitFlags flags{};
    flags.exists = true;
    if (wclType == WclActorType::Player) {
        flags.reaction = UnitReaction::Friendly;
        flags.affiliation = UnitAffiliation::Raid;
        flags.unitType = UnitType::Player;
        flags.controller = UnitController::Player;
    } else if (wclType == WclActorType::NPC || wclType == WclActorType::Boss) {
        flags.reaction = UnitReaction::Hostile;
        flags.affiliation = UnitAffiliation::Outsider;
        flags.unitType = UnitType::NPC;
        flags.controller = UnitController::NPC;
    } else if (wclType == WclActorType::Pet || wclType == WclActorType::Guardian) {
        flags.reaction = UnitReaction::Friendly;
        flags.affiliation = UnitAffiliation::Raid;
        flags.unitType = UnitType::Pet;
        flags.controller = UnitController::Player;
    }
    actorEvent.flags = flags;
    // Colour is looked up at render time by ActorColorGenerator.

    output.actorEvents.push_back(std::move(actorEvent));

    return true;
}

void WclToAwowAdapter::extractResourceData(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    if (!event.HasMember("hitPoints") || !event.HasMember("maxHitPoints")) {
        return;
    }

    // resourceActor: 1 source, 2 target; otherwise prefer the target.
    int resourceActor = getInt(event, "resourceActor", 0);
    int32_t actorId = -1;
    int32_t instanceId = 0;

    if (resourceActor == 1) {
        actorId = getInt(event, "sourceID", -1);
        instanceId = getInt(event, "sourceInstance", 0);
    } else if (resourceActor == 2) {
        actorId = getInt(event, "targetID", -1);
        instanceId = getInt(event, "targetInstance", 0);
    } else {
        if (event.HasMember("targetID")) {
            actorId = getInt(event, "targetID", -1);
            instanceId = getInt(event, "targetInstance", 0);
        } else {
            actorId = getInt(event, "sourceID", -1);
            instanceId = getInt(event, "sourceInstance", 0);
        }
    }

    if (actorId < 0) {
        return;
    }

    ResourceEvent resEvent;
    resEvent.actor_guid = getActorGuidWithInstance(actorId, instanceId, metadata);
    resEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);

    int64_t hitPoints = getInt64(event, "hitPoints");
    resEvent.current_health = hitPoints > 0 ? static_cast<uint64_t>(hitPoints) : 0;

    int64_t maxHitPoints = getInt64(event, "maxHitPoints");
    resEvent.max_health = maxHitPoints > 0 ? static_cast<uint64_t>(maxHitPoints) : 0;

    resEvent.current_power = 0;
    resEvent.max_power = 0;
    resEvent.power_type = -1;

    if (event.HasMember("classResources") && event["classResources"].IsArray()) {
        const auto& resources = event["classResources"];
        for (rapidjson::SizeType i = 0; i < resources.Size(); ++i) {
            const auto& res = resources[i];
            if (i == 0) {
                resEvent.current_power = static_cast<uint32_t>(getInt(res, "amount"));
                resEvent.max_power = static_cast<uint32_t>(getInt(res, "max"));
                resEvent.power_type = static_cast<int8_t>(getInt(res, "type", -1));
            } else {
                // Secondary resources, as local logs keep them.
                resEvent.secondary_powers.push_back(
                    {static_cast<int8_t>(getInt(res, "type", -1)),
                     static_cast<uint32_t>(getInt(res, "amount")),
                     static_cast<uint32_t>(getInt(res, "max"))});
            }
        }
    }

    WclActorType wclType = m_actorMapper.getActorType(actorId);
    resEvent.actor_type = convertActorType(wclType);

    UnitFlags flags{};
    flags.exists = true;
    if (wclType == WclActorType::Player) {
        flags.reaction = UnitReaction::Friendly;
        flags.affiliation = UnitAffiliation::Raid;
        flags.unitType = UnitType::Player;
        flags.controller = UnitController::Player;
    } else if (wclType == WclActorType::NPC || wclType == WclActorType::Boss) {
        flags.reaction = UnitReaction::Hostile;
        flags.affiliation = UnitAffiliation::Outsider;
        flags.unitType = UnitType::NPC;
        flags.controller = UnitController::NPC;
    } else if (wclType == WclActorType::Pet || wclType == WclActorType::Guardian) {
        flags.reaction = UnitReaction::Friendly;
        flags.affiliation = UnitAffiliation::Raid;
        flags.unitType = UnitType::Pet;
        flags.controller = UnitController::Player;
    }
    resEvent.flags = flags;

    output.resourceEvents.push_back(std::move(resEvent));
}

float WclToAwowAdapter::convertCoordinate(int64_t wclCoord) {
    return static_cast<float>(wclCoord) / COORD_SCALE;
}

} // namespace warcraftlogs
