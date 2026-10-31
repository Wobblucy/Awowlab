// Combatant info and cast events, including the cooldown reductions
// that come from spending a resource.

#include "Adapter/WclToAwowAdapter.h"
#include "Adapter/WclJsonHelpers.h"
#include "OutputWriter.h"
#include "SpellCastDatabase.h"
#include "Database/CooldownModifiers/CooldownModifierRegistry.h"
#include "Core/StringInterner.h"

namespace warcraftlogs {

using namespace json;

namespace {

// The first positive cost among the cast's classResources entries, or 0.
int32_t firstResourceCost(const rapidjson::Value& event) {
    if (!event.HasMember("classResources") || !event["classResources"].IsArray()) {
        return 0;
    }
    for (const auto& resource : event["classResources"].GetArray()) {
        int32_t cost = getInt(resource, "cost");
        if (cost > 0) {
            return cost;
        }
    }
    return 0;
}

// The power type of the first classResources entry that names one, or -1.
int8_t firstResourceType(const rapidjson::Value& event) {
    if (!event.HasMember("classResources") || !event["classResources"].IsArray()) {
        return -1;
    }
    for (const auto& resource : event["classResources"].GetArray()) {
        if (resource.HasMember("type") && resource["type"].IsInt()) {
            return static_cast<int8_t>(resource["type"].GetInt());
        }
    }
    return -1;
}

} // namespace

void WclToAwowAdapter::processCombatantInfoEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    ExtractedData& output
) {
    CombatantInfoEvent infoEvent;

    int32_t sourceId = getInt(event, "sourceID");
    infoEvent.player_guid = getActorGuid(sourceId, metadata);
    infoEvent.spec_id = static_cast<uint16_t>(getInt(event, "specID"));
    // Colour is looked up at render time by ActorColorGenerator, not stored here.

    // Not every report carries specID on the event. The report's own actor
    // list always names the spec, so fall back to what was resolved from
    // there rather than leaving the player unspecced - anything keyed on spec
    // (class colors, benchmark classifications) has no other source.
    if (infoEvent.spec_id == 0) {
        for (const auto& actor : metadata.actors) {
            if (actor.id == sourceId && actor.specId != 0) {
                infoEvent.spec_id = actor.specId;
                break;
            }
        }
    }

    // WCL puts the selected talents in "talentTree"; they feed TalentDatabase
    // and the resource-based cooldown modifiers.
    if (event.HasMember("talentTree") && event["talentTree"].IsArray()) {
        const auto& talentTree = event["talentTree"];
        auto& registry = awow::CooldownModifierRegistry::instance();

        for (rapidjson::SizeType i = 0; i < talentTree.Size(); ++i) {
            if (talentTree[i].IsObject() && talentTree[i].HasMember("id")) {
                uint32_t talentId = static_cast<uint32_t>(getInt(talentTree[i], "id"));

                // WCL gives only the talent id, with no spell id or rank. The
                // wcl_talent_ids in the talent data use the same ids, so it fills both.
                parser::TalentEntry entry;
                entry.talent_id = talentId;
                entry.spell_id = talentId;
                entry.rank = 1;
                infoEvent.talents.push_back(entry);

                const auto* modifiers = registry.getModifiersForTalent(talentId);
                if (modifiers) {
                    for (const auto& modifier : *modifiers) {
                        if (modifier.modifier_type == awow::ModifierType::ResourceBased) {
                            int8_t powerType = static_cast<int8_t>(modifier.resource.power_type);
                            m_actorResourceModifiers[infoEvent.player_guid].modifiers_by_power_type[powerType].push_back(modifier);
                        }
                    }
                }
            }
        }
    }

    // Haste ratings as of the start of the fight, used to recalculate haste.
    // Read through GetDouble: a rating that arrives as 1234.0 is still a
    // number, and GetInt on a double is undefined.
    auto readRating = [&event](const char* key, int32_t& out) {
        if (event.HasMember(key) && event[key].IsNumber()) {
            out = static_cast<int32_t>(event[key].GetDouble());
        }
    };
    readRating("hasteMelee", infoEvent.haste_melee);
    readRating("hasteRanged", infoEvent.haste_ranged);
    readRating("hasteSpell", infoEvent.haste_spell);

    // Gear, and the average item level over it. Reports carry every slot,
    // including the ones that do not contribute: an unfilled slot comes through
    // with id 0, and a shirt or tabard as itemLevel 1. Averaging the raw array
    // pulls a 317 ilvl character down to 264, so both are skipped.
    if (event.HasMember("gear") && event["gear"].IsArray()) {
        const auto& gear = event["gear"];
        uint32_t levelSum = 0;
        uint32_t counted = 0;

        infoEvent.gear.reserve(gear.Size());
        for (rapidjson::SizeType i = 0; i < gear.Size(); ++i) {
            const auto& piece = gear[i];
            CombatantInfoEvent::GearPiece slot;
            slot.item_id = static_cast<uint32_t>(getInt(piece, "id"));
            slot.item_level = static_cast<uint16_t>(getInt(piece, "itemLevel"));

            if (slot.item_id == 0) continue;           // empty slot
            infoEvent.gear.push_back(slot);

            if (slot.item_level > 1) {                 // not a cosmetic
                levelSum += slot.item_level;
                ++counted;
            }
        }

        if (counted > 0) {
            infoEvent.item_level = static_cast<uint16_t>(levelSum / counted);
        }
    }

    output.combatantInfoEvents.push_back(std::move(infoEvent));
}

void WclToAwowAdapter::processCastEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    std::string eventType = getString(event, "type");
    int32_t sourceId = getInt(event, "sourceID");
    int32_t sourceInstance = getInt(event, "sourceInstance", 0);
    uint32_t spellId = static_cast<uint32_t>(getInt(event, "abilityGameID"));
    int32_t timestamp = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);

    if (eventType == "begincast") {
        // Held until the matching "cast" arrives.
        auto key = std::make_pair(sourceId, spellId);
        m_pendingCasts[key] = timestamp;
        return;
    }

    SpellCastEvent castEvent;
    castEvent.actor_guid = getActorGuidWithInstance(sourceId, sourceInstance, metadata);
    castEvent.spell_id = spellId;
    castEvent.end_timestamp_ms = timestamp;
    castEvent.cancelled = false;
    castEvent.haste_permille = 0;

    castEvent.power_cost = firstResourceCost(event);
    if (castEvent.power_cost > 0) {
        addResourceCooldownReductions(event, castEvent, timestamp, output);
    }

    // Needed by auto-annotation triggers anchored on the target.
    int32_t targetId = getInt(event, "targetID", -1);
    if (targetId > 0) {
        int32_t targetInstance = getInt(event, "targetInstance", 0);
        castEvent.target_guid = getActorGuidWithInstance(targetId, targetInstance, metadata);
    }

    auto key = std::make_pair(sourceId, spellId);
    auto it = m_pendingCasts.find(key);
    if (it != m_pendingCasts.end()) {
        // A preceding begincast means a hard cast; otherwise it was instant.
        castEvent.start_timestamp_ms = it->second;
        m_pendingCasts.erase(it);
    } else {
        castEvent.start_timestamp_ms = timestamp;
    }

    output.spellCastEvents.push_back(std::move(castEvent));
}

void WclToAwowAdapter::addResourceCooldownReductions(
    const rapidjson::Value& event,
    const SpellCastEvent& cast,
    int32_t timestamp,
    ExtractedData& output
) {
    auto actorIt = m_actorResourceModifiers.find(cast.actor_guid);
    if (actorIt == m_actorResourceModifiers.end()) {
        return;
    }

    int8_t powerType = firstResourceType(event);
    if (powerType < 0) {
        return;
    }

    const auto& modifiersByPowerType = actorIt->second.modifiers_by_power_type;
    auto powerTypeIt = modifiersByPowerType.find(powerType);
    if (powerTypeIt == modifiersByPowerType.end()) {
        return;
    }

    uint32_t actorGuidId = guidInterner().intern(cast.actor_guid);

    for (const auto& modifier : powerTypeIt->second) {
        if (modifier.resource.resource_per_second <= 0) continue;
        CooldownReductionRecord record;
        record.actor_guid_id = actorGuidId;
        record.spell_id = modifier.modified_spell_id;
        record.timestamp_ms = timestamp;
        record.reduction_ms = (cast.power_cost * 1000) / modifier.resource.resource_per_second;
        output.cooldownReductions.push_back(record);
    }
}

} // namespace warcraftlogs
