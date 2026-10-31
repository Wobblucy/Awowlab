// Damage, healing and absorb events. Misses arrive as damage events and are split off here.

#include "Adapter/WclToAwowAdapter.h"
#include "Adapter/WclJsonHelpers.h"
#include "OutputWriter.h"
#include "SpellCastDatabase.h"
#include "Core/StringInterner.h"
#include "Parser/Include/Structures/CombatTypes.h"

namespace warcraftlogs {

using namespace json;

MissType WclToAwowAdapter::missTypeFromHitType(int32_t hitType) {
    // Warcraft Logs sends no "miss" event. An attack that didn't land arrives
    // as a damage event with a hitType and an amount of zero. The documentation
    // doesn't name the values; these come from the cached reports:
    //
    //   1, 2   land (hit and crit); 97% carry unmitigatedAmount, amount > 0
    //   4      partial absorb; 87% carry absorbed, all mitigated, amount > 0
    //   0      always amount 0, across seven targets, mostly melee
    //   7, 8   always amount 0, all melee, each on a single target
    //   10     always amount 0, spells only (Charge, Storm Bolt, Holy Word:
    //          Chastise on bosses), i.e. stuns on a stun-immune target
    //
    // 7 and 8 each land on one target, the tank, the only raider who can dodge
    // or parry the boss. That identifies the pair but not which is which, so
    // both map to DODGE rather than a guess between DODGE and PARRY.
    switch (hitType) {
        case 0:  return MissType::MISS;
        case 7:  return MissType::DODGE;
        case 8:  return MissType::DODGE;
        case 10: return MissType::IMMUNE;
        default: return MissType::Unknown;
    }
}

void WclToAwowAdapter::processDamageEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    // An attack that did not land is a damage event with a non-landing hitType
    // and no amount. Recorded as a miss and not as damage, or a wipe's worth of
    // dodges would read as zero-damage hits.
    const MissType missType = missTypeFromHitType(getInt(event, "hitType", 1));
    if (missType != MissType::Unknown) {
        MissedEvent missedEvent;

        const int32_t missSourceId = getInt(event, "sourceID");
        const int32_t missTargetId = getInt(event, "targetID");
        missedEvent.source_guid = getActorGuid(missSourceId, metadata);
        missedEvent.source_name = getActorName(missSourceId, metadata);
        missedEvent.target_guid = getActorGuid(missTargetId, metadata);
        missedEvent.target_name = getActorName(missTargetId, metadata);

        missedEvent.spell_id = static_cast<uint32_t>(getInt(event, "abilityGameID"));
        missedEvent.spell_name = "";   // named later from the spell database
        missedEvent.timestamp_ms =
            convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
        missedEvent.miss_type = missType;
        missedEvent.amount_missed = 0;  // nothing landed, so nothing was absorbed
        missedEvent.blocked_amount = 0;
        missedEvent.is_offhand = false;
        missedEvent.is_critical = false;
        missedEvent.is_periodic = event.HasMember("tick") &&
                                  event["tick"].IsBool() && event["tick"].GetBool();

        output.missedEvents.push_back(std::move(missedEvent));
        return;
    }

    CombatEvent combatEvent;

    int32_t sourceId = getInt(event, "sourceID");
    int32_t targetId = getInt(event, "targetID");

    combatEvent.source_guid_id = guidInterner().intern(getActorGuid(sourceId, metadata));
    combatEvent.target_guid_id = guidInterner().intern(getActorGuid(targetId, metadata));

    // sourceOwner catches pets summoned before the fight, which the summon events miss.
    int32_t sourceOwner = getInt(event, "sourceOwner", -1);
    if (sourceOwner > 0 && m_petToOwnerMap.find(sourceId) == m_petToOwnerMap.end()) {
        m_petToOwnerMap[sourceId] = sourceOwner;
    }

    auto ownerIt = m_petToOwnerMap.find(sourceId);
    if (ownerIt != m_petToOwnerMap.end()) {
        combatEvent.owner_guid_id = guidInterner().intern(getActorGuid(ownerIt->second, metadata));
    } else {
        combatEvent.owner_guid_id = 0;
    }

    combatEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    combatEvent.spell_id = getSpellId(event);

    int64_t amount = getInt64(event, "amount");
    int64_t unmitigated = getInt64(event, "unmitigatedAmount", amount);
    int64_t absorbed = getInt64(event, "absorbed");
    int64_t blocked = getInt64(event, "blocked");

    // hitType 1 is a normal hit, 2 a crit.
    int hitType = getInt(event, "hitType", 1);
    bool isCrit = (hitType == 2);

    bool isTick = getBool(event, "tick", false);

    // Friendly fire (self-damage, Spirit Link, etc.) stays a damage
    // event with the FriendlyFire flag, mirroring the local pipeline:
    // it counts as damage taken by the target but not as damage done.
    // WCL doesn't ship per-event unit flags, so "friendly" means a
    // player or a pet a player owns - the only friendly-fire sources
    // in practice.
    auto friendlyActor = [&](int32_t actorId) {
        std::string guid = getActorGuid(actorId, metadata);
        if (std::string_view(guid).starts_with("Player-")) {
            return true;
        }
        auto petIt = m_petToOwnerMap.find(actorId);
        if (petIt == m_petToOwnerMap.end()) {
            return false;
        }
        std::string ownerGuid = getActorGuid(petIt->second, metadata);
        return std::string_view(ownerGuid).starts_with("Player-");
    };
    bool isFriendlyFire = friendlyActor(sourceId) && friendlyActor(targetId);

    // Doubling the base for crits approximates the post-crit value, so
    // amount - effective_amount shows mitigation rather than the crit bonus.
    combatEvent.amount = isCrit ? (unmitigated * 2) : unmitigated;
    combatEvent.effective_amount = amount;
    combatEvent.absorbed = absorbed;
    combatEvent.blocked = blocked;
    combatEvent.resisted = 0;  // WCL doesn't provide magic resistance data

    uint16_t flags = 0;

    if (isCrit) {
        flags |= CombatEventFlags::Critical;
    }

    if (isTick) {
        flags |= CombatEventFlags::Periodic;
    }

    // supportID marks damage credited through an Augmentation Evoker's buff.
    if (event.HasMember("supportID")) {
        flags |= CombatEventFlags::Support;
    }

    // spell_id 1 is melee in both WCL and local logs.
    if (combatEvent.spell_id == 1) {
        flags |= CombatEventFlags::IsSwing;
    }

    if (isFriendlyFire) {
        flags |= CombatEventFlags::FriendlyFire;
    }

    combatEvent.flags = flags;
    combatEvent.event_type = CombatEventType::DamageDealt;
    combatEvent.spell_school = 0;  // WarcraftLogs doesn't provide this directly

    output.combatEvents.push_back(std::move(combatEvent));
}

void WclToAwowAdapter::processHealEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    CombatEvent combatEvent;

    int32_t sourceId = getInt(event, "sourceID");
    int32_t targetId = getInt(event, "targetID");

    combatEvent.source_guid_id = guidInterner().intern(getActorGuid(sourceId, metadata));
    combatEvent.target_guid_id = guidInterner().intern(getActorGuid(targetId, metadata));

    // sourceOwner catches pets summoned before the fight, which the summon events miss.
    int32_t sourceOwner = getInt(event, "sourceOwner", -1);
    if (sourceOwner > 0 && m_petToOwnerMap.find(sourceId) == m_petToOwnerMap.end()) {
        m_petToOwnerMap[sourceId] = sourceOwner;
    }

    auto ownerIt = m_petToOwnerMap.find(sourceId);
    if (ownerIt != m_petToOwnerMap.end()) {
        combatEvent.owner_guid_id = guidInterner().intern(getActorGuid(ownerIt->second, metadata));
    } else {
        combatEvent.owner_guid_id = 0;
    }

    combatEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    combatEvent.spell_id = getSpellId(event);

    int64_t amount = getInt64(event, "amount");
    int64_t overheal = getSanitizedInt64(event, "overheal");
    int64_t absorbed = getSanitizedInt64(event, "absorbed");  // Healing absorbed by debuffs

    combatEvent.amount = amount + overheal;  // Total healing attempted
    combatEvent.effective_amount = amount;
    combatEvent.absorbed = absorbed;
    combatEvent.blocked = 0;
    combatEvent.resisted = 0;

    uint16_t flags = 0;

    int hitType = getInt(event, "hitType", 1);
    if (hitType == 2) {
        flags |= CombatEventFlags::Critical;
    }

    bool isTick = getBool(event, "tick", false);
    if (isTick) {
        flags |= CombatEventFlags::Periodic;
    }

    // supportID marks healing credited through an Augmentation Evoker's buff.
    if (event.HasMember("supportID")) {
        flags |= CombatEventFlags::Support;
    }

    combatEvent.flags = flags;
    combatEvent.event_type = CombatEventType::HealingDone;
    combatEvent.spell_school = 0;  // WarcraftLogs doesn't provide this

    output.combatEvents.push_back(std::move(combatEvent));
}

void WclToAwowAdapter::processAbsorbedEvent(
    const rapidjson::Value& event,
    const WclReportMetadata& metadata,
    uint64_t fightStartTime,
    ExtractedData& output
) {
    // WCL "absorbed" events are damage soaked by a shield (PW:S, AMS, ...).
    // They count as healing done by the caster (sourceID) to the protected
    // player (targetID).
    CombatEvent combatEvent;

    int32_t sourceId = getInt(event, "sourceID");  // Shield caster
    int32_t targetId = getInt(event, "targetID");  // Protected player

    combatEvent.source_guid_id = guidInterner().intern(getActorGuid(sourceId, metadata));
    combatEvent.target_guid_id = guidInterner().intern(getActorGuid(targetId, metadata));

    // sourceOwner catches pets summoned before the fight, which the summon events miss.
    int32_t sourceOwner = getInt(event, "sourceOwner", -1);
    if (sourceOwner > 0 && m_petToOwnerMap.find(sourceId) == m_petToOwnerMap.end()) {
        m_petToOwnerMap[sourceId] = sourceOwner;
    }

    auto ownerIt = m_petToOwnerMap.find(sourceId);
    if (ownerIt != m_petToOwnerMap.end()) {
        combatEvent.owner_guid_id = guidInterner().intern(getActorGuid(ownerIt->second, metadata));
    } else {
        combatEvent.owner_guid_id = 0;
    }

    combatEvent.timestamp_ms = convertTimestamp(getUint64(event, "timestamp"), fightStartTime);
    combatEvent.spell_id = getSpellId(event);

    int64_t amount = getInt64(event, "amount");

    combatEvent.amount = amount;
    combatEvent.effective_amount = amount;
    combatEvent.absorbed = 0;
    combatEvent.blocked = 0;
    combatEvent.resisted = 0;

    // Absorbs are never crits or periodic.
    uint16_t flags = 0;

    combatEvent.flags = flags;
    combatEvent.event_type = CombatEventType::HealingDone;
    combatEvent.spell_school = 0;  // WarcraftLogs doesn't provide this

    output.combatEvents.push_back(std::move(combatEvent));
}

} // namespace warcraftlogs
