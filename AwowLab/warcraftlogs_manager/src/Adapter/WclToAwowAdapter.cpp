// Report metadata parsing, event conversion and page merging.

#include "Adapter/WclToAwowAdapter.h"
#include "Adapter/WclJsonHelpers.h"
#include "OutputWriter.h"
#include "AuraDatabase.h"
#include "SpellCastDatabase.h"

#include "Parser/SpecializationColors.h"

#include <rapidjson/error/en.h>
#include <algorithm>
#include <stdexcept>

namespace warcraftlogs {

using namespace json;

WclToAwowAdapter::WclToAwowAdapter() = default;
WclToAwowAdapter::~WclToAwowAdapter() = default;

WclReportMetadata WclToAwowAdapter::parseReportMetadata(const rapidjson::Document& doc) {
    WclReportMetadata metadata;

    metadata.reportCode = getString(doc, "report_code");

    // Unix epoch ms.
    if (auto startTime = getTimestamp(doc, "startTime")) {
        metadata.startTime = *startTime;
    }

    if (doc.HasMember("fights") && doc["fights"].IsArray()) {
        const auto& fights = doc["fights"];
        metadata.fights.reserve(fights.Size());

        for (rapidjson::SizeType i = 0; i < fights.Size(); ++i) {
            const auto& f = fights[i];
            WclFight fight;
            fight.id = getInt(f, "id");
            fight.name = getString(f, "name");
            fight.startTime = getUint64(f, "startTime");
            fight.endTime = getUint64(f, "endTime");
            fight.encounterID = getInt(f, "encounterID");
            fight.difficulty = getInt(f, "difficulty");
            fight.size = getInt(f, "size", 20);
            fight.keystoneLevel = getInt(f, "keystoneLevel");
            fight.kill = getBool(f, "kill", fight.kill);
            metadata.fights.push_back(std::move(fight));
        }
    }

    if (doc.HasMember("actors") && doc["actors"].IsArray()) {
        const auto& actors = doc["actors"];
        metadata.actors.reserve(actors.Size());

        for (rapidjson::SizeType i = 0; i < actors.Size(); ++i) {
            const auto& a = actors[i];
            WclActor actor;
            actor.id = getInt(a, "id");
            actor.name = getString(a, "name");
            actor.type = getString(a, "type");
            actor.actorType = parseActorType(actor.type);
            actor.petOwner = getInt(a, "petOwner", -1);

            // gameID is the real WoW creature id. WCL sends it as a JSON number
            // (sometimes a double), so read it loosely.
            if (a.HasMember("gameID") && a["gameID"].IsNumber()) {
                const double gid = a["gameID"].GetDouble();
                if (gid > 0.0)
                    actor.gameId = static_cast<uint32_t>(gid);
            }

            // Imported reports carry no COMBATANT_INFO, so a player's spec is
            // only available here: subType holds the class and icon holds
            // "Class-Spec". Anything keyed on spec id has no other source.
            actor.className = getString(a, "subType");
            const std::string icon = getString(a, "icon");
            if (const size_t dash = icon.find('-'); dash != std::string::npos) {
                actor.specName = icon.substr(dash + 1);
            }
            if (!actor.specName.empty()) {
                actor.specId = parser::specIdFromNames(actor.className,
                                                       actor.specName);
            }

            actor.guid = m_actorMapper.registerActor(actor.id, actor.actorType, actor.name);

            metadata.actorIdToName[actor.id] = actor.name;
            metadata.actorIdToGuid[actor.id] = actor.guid;
            if (actor.gameId != 0)
                metadata.actorIdToNpcId[actor.id] = actor.gameId;

            metadata.actors.push_back(std::move(actor));
        }
    }

    // An actor_map, when present, adds actors the list did not name.
    if (doc.HasMember("actor_map") && doc["actor_map"].IsObject()) {
        const auto& actorMap = doc["actor_map"];
        for (auto it = actorMap.MemberBegin(); it != actorMap.MemberEnd(); ++it) {
            auto parsedId = json::parseIdKey(it->name);
            if (!parsedId || !it->value.IsString()) {
                continue;
            }
            int32_t actorId = *parsedId;
            std::string name(it->value.GetString(), it->value.GetStringLength());

            if (metadata.actorIdToName.find(actorId) == metadata.actorIdToName.end()) {
                metadata.actorIdToName[actorId] = name;

                std::string guid = m_actorMapper.getGuid(actorId);
                if (guid.empty()) {
                    guid = m_actorMapper.registerActor(actorId, WclActorType::Unknown, name);
                }
                metadata.actorIdToGuid[actorId] = guid;
            }
        }
    }

    return metadata;
}

WclReportMetadata WclToAwowAdapter::parseReportMetadata(const std::string& jsonString) {
    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());

    if (doc.HasParseError()) {
        throw std::runtime_error(
            std::string("Failed to parse report metadata JSON: ") +
            rapidjson::GetParseError_En(doc.GetParseError())
        );
    }

    return parseReportMetadata(doc);
}

ExtractedData WclToAwowAdapter::convertEvents(
    const rapidjson::Document& doc,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ConversionProgressCallback progressCallback
) {
    ExtractedData output;

    if (!doc.IsArray()) {
        throw std::runtime_error("Events JSON must be an array");
    }

    m_pendingCasts.clear();
    m_petToOwnerMap.clear();
    m_currentMapId = 0;
    // m_actorResourceModifiers is kept: combatant info is on the first page,
    // but the casts it applies to span every page.

    // The actor metadata is the authoritative pet-to-owner source, and also
    // covers pets summoned before the fight or without a summon event.
    for (const auto& actor : metadata.actors) {
        if (actor.petOwner > 0) {
            m_petToOwnerMap[actor.id] = actor.petOwner;
        }
    }

    size_t total = doc.Size();
    size_t current = 0;

    // First pass: combatant info (talent-based cooldown modifiers) and summons
    // (pet owners missing from the metadata), which the second pass relies on.
    for (rapidjson::SizeType i = 0; i < doc.Size(); ++i) {
        const auto& event = doc[i];
        if (event.HasMember("type") && event["type"].IsString()) {
            std::string eventType = event["type"].GetString();
            if (eventType == "combatantinfo") {
                processCombatantInfoEvent(event, metadata, output);
            } else if (eventType == "summon") {
                processSummonEvent(event, metadata, fightStartTime, output);
            }
        }
    }

    for (rapidjson::SizeType i = 0; i < doc.Size(); ++i) {
        const auto& event = doc[i];
        if (event.HasMember("type") && event["type"].IsString()) {
            std::string eventType = event["type"].GetString();
            if (eventType != "combatantinfo" && eventType != "summon") {
                processEvent(doc[i], metadata, fightStartTime, output);
            }
        }

        if (progressCallback && (++current % 1000 == 0)) {
            progressCallback(current, total, "Processing events...");
        }
    }

    // A begincast with no matching cast was cancelled.
    for (const auto& [key, startTime] : m_pendingCasts) {
        SpellCastEvent cancelledCast;
        cancelledCast.actor_guid = getActorGuid(key.first, metadata);
        cancelledCast.spell_id = key.second;
        cancelledCast.start_timestamp_ms = startTime;
        cancelledCast.end_timestamp_ms = 0;
        cancelledCast.cancelled = true;
        cancelledCast.haste_permille = 0;
        output.spellCastEvents.push_back(std::move(cancelledCast));
    }
    m_pendingCasts.clear();

    if (progressCallback) {
        progressCallback(total, total, "Conversion complete");
    }

    // Imported reports need haste recalculated.
    output.fromImportedReport = true;

    // guid -> name for the UI, from actorIdToGuid and actorIdToName.
    for (const auto& [actorId, guid] : metadata.actorIdToGuid) {
        auto nameIt = metadata.actorIdToName.find(actorId);
        if (nameIt != metadata.actorIdToName.end()) {
            output.guidToNameMap[guid] = nameIt->second;
        }
    }

    // Carry the real creature ids: the synthetic GUIDs above encode WCL actor
    // ids, so anything keyed on npc id needs this side map.
    output.actorIdToNpcId = metadata.actorIdToNpcId;

    // Spec back-fill happens in mergePages, not here: this runs once per page,
    // and a row per player per page would multiply the roster by the page
    // count.

    return output;
}

ExtractedData WclToAwowAdapter::convertEvents(
    const std::string& jsonString,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ConversionProgressCallback progressCallback
) {
    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());

    if (doc.HasParseError()) {
        throw std::runtime_error(
            std::string("Failed to parse events JSON: ") +
            rapidjson::GetParseError_En(doc.GetParseError())
        );
    }

    return convertEvents(doc, metadata, fightStartTime, progressCallback);
}

ExtractedData WclToAwowAdapter::mergePages(const std::vector<ExtractedData>& pages) {
    ExtractedData merged;

    size_t totalActorEvents = 0;
    size_t totalCombatEvents = 0;
    size_t totalDeathEvents = 0;
    size_t totalAuraEvents = 0;
    size_t totalSpellCastEvents = 0;
    size_t totalResourceEvents = 0;
    size_t totalCombatantInfoEvents = 0;
    size_t totalResurrectEvents = 0;
    size_t totalSummonEvents = 0;
    size_t totalMissedEvents = 0;
    size_t totalAbsorbEvents = 0;
    size_t totalDispelInterruptEvents = 0;
    size_t totalCooldownReductions = 0;

    for (const auto& page : pages) {
        totalActorEvents += page.actorEvents.size();
        totalCombatEvents += page.combatEvents.size();
        totalDeathEvents += page.deathEvents.size();
        totalAuraEvents += page.auraEvents.size();
        totalSpellCastEvents += page.spellCastEvents.size();
        totalResourceEvents += page.resourceEvents.size();
        totalCombatantInfoEvents += page.combatantInfoEvents.size();
        totalResurrectEvents += page.resurrectEvents.size();
        totalSummonEvents += page.summonEvents.size();
        totalMissedEvents += page.missedEvents.size();
        totalAbsorbEvents += page.absorbEvents.size();
        totalDispelInterruptEvents += page.dispelInterruptEvents.size();
        totalCooldownReductions += page.cooldownReductions.size();
    }

    merged.actorEvents.reserve(totalActorEvents);
    merged.combatEvents.reserve(totalCombatEvents);
    merged.deathEvents.reserve(totalDeathEvents);
    merged.auraEvents.reserve(totalAuraEvents);
    merged.spellCastEvents.reserve(totalSpellCastEvents);
    merged.resourceEvents.reserve(totalResourceEvents);
    merged.combatantInfoEvents.reserve(totalCombatantInfoEvents);
    merged.resurrectEvents.reserve(totalResurrectEvents);
    merged.summonEvents.reserve(totalSummonEvents);
    merged.missedEvents.reserve(totalMissedEvents);
    merged.absorbEvents.reserve(totalAbsorbEvents);
    merged.dispelInterruptEvents.reserve(totalDispelInterruptEvents);
    merged.cooldownReductions.reserve(totalCooldownReductions);

    for (const auto& page : pages) {
        merged.actorEvents.insert(merged.actorEvents.end(),
            page.actorEvents.begin(), page.actorEvents.end());
        merged.combatEvents.insert(merged.combatEvents.end(),
            page.combatEvents.begin(), page.combatEvents.end());
        merged.deathEvents.insert(merged.deathEvents.end(),
            page.deathEvents.begin(), page.deathEvents.end());
        merged.auraEvents.insert(merged.auraEvents.end(),
            page.auraEvents.begin(), page.auraEvents.end());
        merged.spellCastEvents.insert(merged.spellCastEvents.end(),
            page.spellCastEvents.begin(), page.spellCastEvents.end());
        merged.resourceEvents.insert(merged.resourceEvents.end(),
            page.resourceEvents.begin(), page.resourceEvents.end());
        merged.combatantInfoEvents.insert(merged.combatantInfoEvents.end(),
            page.combatantInfoEvents.begin(), page.combatantInfoEvents.end());
        merged.resurrectEvents.insert(merged.resurrectEvents.end(),
            page.resurrectEvents.begin(), page.resurrectEvents.end());
        merged.summonEvents.insert(merged.summonEvents.end(),
            page.summonEvents.begin(), page.summonEvents.end());
        merged.missedEvents.insert(merged.missedEvents.end(),
            page.missedEvents.begin(), page.missedEvents.end());
        merged.absorbEvents.insert(merged.absorbEvents.end(),
            page.absorbEvents.begin(), page.absorbEvents.end());
        merged.dispelInterruptEvents.insert(merged.dispelInterruptEvents.end(),
            page.dispelInterruptEvents.begin(), page.dispelInterruptEvents.end());
        merged.cooldownReductions.insert(merged.cooldownReductions.end(),
            page.cooldownReductions.begin(), page.cooldownReductions.end());
        merged.energizeEvents.insert(merged.energizeEvents.end(),
            page.energizeEvents.begin(), page.energizeEvents.end());

        // Map changes match across pages; keep the first non-empty set.
        if (merged.mapChangeEvents.empty() && !page.mapChangeEvents.empty()) {
            merged.mapChangeEvents = page.mapChangeEvents;
        }

        for (const auto& [spellId, spellName] : page.spellNameMap) {
            merged.spellNameMap[spellId] = spellName;
        }

        for (const auto& [actorId, npcId] : page.actorIdToNpcId) {
            merged.actorIdToNpcId[actorId] = npcId;
        }
        for (const auto& [guid, name] : page.guidToNameMap) {
            merged.guidToNameMap[guid] = name;
        }

        for (const auto& [classId, spells] : page.spellsSeenByClass.spellsByClass) {
            for (const auto& [spellId, spellName] : spells) {
                merged.spellsSeenByClass.spellsByClass[classId][spellId] = spellName;
            }
        }
        for (const auto& [classId, auras] : page.aurasSeenByClass.aurasByClass) {
            for (const auto& [spellId, auraInfo] : auras) {
                merged.aurasSeenByClass.aurasByClass[classId][spellId] = auraInfo;
            }
        }
    }

    // Every page carries the same imported-report flag.
    if (!pages.empty()) {
        merged.fromImportedReport = pages[0].fromImportedReport;
    }

    return merged;
}

void WclToAwowAdapter::processEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    if (!event.HasMember("type") || !event["type"].IsString()) {
        return;
    }

    std::string eventType = event["type"].GetString();

    extractPositionData(event, metadata, fightStartTime, output);

    extractResourceData(event, metadata, fightStartTime, output);

    if (eventType == "damage") {
        processDamageEvent(event, metadata, fightStartTime, output);
    }
    else if (eventType == "heal") {
        processHealEvent(event, metadata, fightStartTime, output);
    }
    else if (eventType == "death") {
        processDeathEvent(event, metadata, fightStartTime, output);
    }
    else if (eventType == "resurrect") {
        processResurrectEvent(event, metadata, fightStartTime, output);
    }
    else if (eventType == "applybuff" || eventType == "applydebuff") {
        processAuraEvent(event, metadata, fightStartTime, output, AuraEventType::Applied);
    }
    else if (eventType == "removebuff" || eventType == "removedebuff") {
        processAuraEvent(event, metadata, fightStartTime, output, AuraEventType::Removed);
    }
    else if (eventType == "refreshbuff" || eventType == "refreshdebuff") {
        processAuraEvent(event, metadata, fightStartTime, output, AuraEventType::Refresh);
    }
    else if (eventType == "applybuffstack" || eventType == "applydebuffstack") {
        processAuraEvent(event, metadata, fightStartTime, output, AuraEventType::AppliedDose);
    }
    else if (eventType == "removebuffstack" || eventType == "removedebuffstack") {
        processAuraEvent(event, metadata, fightStartTime, output, AuraEventType::RemovedDose);
    }
    else if (eventType == "cast" || eventType == "begincast") {
        processCastEvent(event, metadata, fightStartTime, output);
    }
    else if (eventType == "resourcechange") {
        processResourceChangeEvent(event, metadata, fightStartTime, output);
    }
    // There is no "miss" type: an attack that didn't land is a damage event
    // with a non-landing hitType, which processDamageEvent records as a
    // MissedEvent.
    //
    // "healabsorbed" (a heal eaten by an absorb debuff, with healerID and
    // extraAbilityGameID) is left out on purpose. AbsorbEvent has no place for
    // either field and the local parser doesn't produce it, so it would need an
    // event type of its own.
    else if (eventType == "absorbed") {
        processAbsorbedEvent(event, metadata, fightStartTime, output);
    }
    // combatantinfo and summon were handled in the first pass of convertEvents;
    // other types (encounterstart/end and so on) are ignored.
}

std::string WclToAwowAdapter::getActorGuid(int32_t actorId, const WclReportMetadata& metadata) const {
    // Environment damage and other sourceless events use actor id -1.
    if (actorId == -1) {
        return "0000000000000000";
    }

    auto it = metadata.actorIdToGuid.find(actorId);
    if (it != metadata.actorIdToGuid.end()) {
        return it->second;
    }

    // Not in the metadata, so generate one.
    return GuidGenerator::generate(actorId, WclActorType::Unknown);
}

std::string WclToAwowAdapter::getActorGuidWithInstance(int32_t actorId, int32_t instanceId, const WclReportMetadata& metadata) const {
    // Instance 0 means a single spawn and uses the plain GUID. Later spawns get
    // "{BaseGUID}-{InstanceId}" so each stays distinct.
    if (instanceId <= 0) {
        return getActorGuid(actorId, metadata);
    }

    std::string baseGuid = getActorGuid(actorId, metadata);
    return baseGuid + "-" + std::to_string(instanceId);
}

std::string WclToAwowAdapter::getActorName(int32_t actorId, const WclReportMetadata& metadata) const {
    auto it = metadata.actorIdToName.find(actorId);
    if (it != metadata.actorIdToName.end()) {
        return it->second;
    }
    return "Unknown";
}

std::string WclToAwowAdapter::getActorNameWithInstance(int32_t actorId, int32_t instanceId, const WclReportMetadata& metadata) const {
    std::string baseName = getActorName(actorId, metadata);

    // Instance 0 uses the plain name; later spawns get the instance number.
    if (instanceId <= 0) {
        return baseName;
    }

    return baseName + " (" + std::to_string(instanceId) + ")";
}

ActorType WclToAwowAdapter::convertActorType(WclActorType wclType) const {
    switch (wclType) {
        case WclActorType::Player:
            return ActorType::Player;
        case WclActorType::NPC:
        case WclActorType::Boss:
            return ActorType::Creature;
        case WclActorType::Pet:
        case WclActorType::Guardian:
            return ActorType::Pet;
        default:
            return ActorType::Unknown;
    }
}

int32_t WclToAwowAdapter::convertTimestamp(uint64_t wclTimestamp, uint64_t fightStartTime) {
    int64_t relative = static_cast<int64_t>(wclTimestamp) - static_cast<int64_t>(fightStartTime);
    return static_cast<int32_t>(relative);
}

} // namespace warcraftlogs
