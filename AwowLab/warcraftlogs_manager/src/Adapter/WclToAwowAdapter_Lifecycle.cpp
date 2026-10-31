// Death, resurrect, resource gain, summon and aura events.

#include "Adapter/WclToAwowAdapter.h"
#include "Adapter/WclJsonHelpers.h"
#include "OutputWriter.h"
#include "AuraDatabase.h"

namespace warcraftlogs {

using namespace json;

void WclToAwowAdapter::processDeathEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    DeathEvent deathEvent;

    int32_t targetId = getInt(event, "targetID");
    int32_t instanceId = getInt(event, "targetInstance");

    // Instance-aware GUID and name, so these match the position events.
    deathEvent.actor_guid = getActorGuidWithInstance(targetId, instanceId, metadata);
    deathEvent.actor_name = getActorNameWithInstance(targetId, instanceId, metadata);
    deathEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);

    deathEvent.killing_spell_id = static_cast<uint32_t>(getInt(event, "killingAbilityGameID"));

    WclActorType wclType = m_actorMapper.getActorType(targetId);
    deathEvent.actor_type = convertActorType(wclType);

    output.deathEvents.push_back(std::move(deathEvent));
}

void WclToAwowAdapter::processResurrectEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    ResurrectEvent resEvent;

    int32_t targetId = getInt(event, "targetID");
    int32_t instanceId = getInt(event, "targetInstance");

    // Instance-aware GUID and name, so these match the position events.
    resEvent.actor_guid = getActorGuidWithInstance(targetId, instanceId, metadata);
    resEvent.actor_name = getActorNameWithInstance(targetId, instanceId, metadata);
    resEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    resEvent.spell_id = static_cast<uint32_t>(getInt(event, "abilityGameID"));

    output.resurrectEvents.push_back(std::move(resEvent));
}

void WclToAwowAdapter::processResourceChangeEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    EnergizeEvent energize;

    // The resource belongs to whoever gained it, which WCL reports as the
    // target - a spell can hand resource to someone other than its caster.
    const int32_t targetId = getInt(event, "targetID");
    const int32_t instanceId = getInt(event, "targetInstance");

    energize.actor_guid = getActorGuidWithInstance(targetId, instanceId, metadata);
    energize.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    energize.spell_id = static_cast<uint32_t>(getInt(event, "abilityGameID"));
    energize.power_type = static_cast<int8_t>(getInt(event, "resourceChangeType", -1));

    // WCL reports the gain including the wasted part, where a local log's
    // amount is the gain and over_energize the excess on top. Subtracting keeps
    // both sources meaning the same thing in the exported table.
    const double reported = static_cast<double>(getInt(event, "resourceChange"));
    const double waste = static_cast<double>(getInt(event, "waste"));
    energize.amount = reported - waste;
    energize.overflow = waste;

    output.energizeEvents.push_back(std::move(energize));
}

void WclToAwowAdapter::processSummonEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    SummonEvent summonEvent;

    int32_t sourceId = getInt(event, "sourceID");
    int32_t targetId = getInt(event, "targetID");

    summonEvent.summoner_guid = getActorGuid(sourceId, metadata);
    summonEvent.summoned_guid = getActorGuid(targetId, metadata);
    summonEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    summonEvent.spell_id = static_cast<uint32_t>(getInt(event, "abilityGameID"));

    // Summons are only a fallback for pet owners; the actor metadata wins.
    if (m_petToOwnerMap.find(targetId) == m_petToOwnerMap.end()) {
        m_petToOwnerMap[targetId] = sourceId;
    }

    output.summonEvents.push_back(std::move(summonEvent));
}

void WclToAwowAdapter::processAuraEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output,
    AuraEventType eventType
) {
    AuraEvent auraEvent;

    int32_t sourceId = getInt(event, "sourceID");
    int32_t targetId = getInt(event, "targetID");
    int32_t sourceInstance = getInt(event, "sourceInstance", 0);
    int32_t targetInstance = getInt(event, "targetInstance", 0);

    // Instance-aware GUIDs, so these match the position events.
    auraEvent.source_guid = getActorGuidWithInstance(sourceId, sourceInstance, metadata);
    auraEvent.target_guid = getActorGuidWithInstance(targetId, targetInstance, metadata);
    auraEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    auraEvent.spell_id = static_cast<uint32_t>(getInt(event, "abilityGameID"));

    std::string wclEventType = getString(event, "type");
    // "removedebuff" contains "buff" too, so test for "debuff" first.
    if (wclEventType.find("debuff") != std::string::npos) {
        auraEvent.aura_type = AuraType::DEBUFF;
    } else {
        auraEvent.aura_type = AuraType::BUFF;
    }

    auraEvent.event_type = eventType;
    auraEvent.stacks = static_cast<uint8_t>(getInt(event, "stack", 1));

    output.auraEvents.push_back(std::move(auraEvent));
}

} // namespace warcraftlogs
