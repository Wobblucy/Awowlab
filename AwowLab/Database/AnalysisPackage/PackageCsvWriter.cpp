#include "PackageCsvWriter.h"

#include "../EventTypes.h"
#include "../AuraDatabase.h"
#include "../SpellCastEvent.h"
#include "CsvUtil.h"
#include "Core/StringInterner.h"

#include <fstream>
#include <unordered_set>

namespace awow::analysis {
namespace {

const char* combatEventTypeName(CombatEventType t) {
    switch (t) {
        case CombatEventType::DamageDealt: return "damage";
        case CombatEventType::HealingDone: return "healing";
    }
    return "unknown";
}

const char* missTypeName(MissType t) {
    switch (t) {
        case MissType::ABSORB:  return "absorb";
        case MissType::BLOCK:   return "block";
        case MissType::DEFLECT: return "deflect";
        case MissType::DODGE:   return "dodge";
        case MissType::EVADE:   return "evade";
        case MissType::IMMUNE:  return "immune";
        case MissType::MISS:    return "miss";
        case MissType::PARRY:   return "parry";
        case MissType::REFLECT: return "reflect";
        case MissType::RESIST:  return "resist";
        case MissType::Unknown: break;
    }
    return "unknown";
}

const char* auraTypeName(AuraType t) {
    switch (t) {
        case AuraType::BUFF:    return "buff";
        case AuraType::DEBUFF:  return "debuff";
        case AuraType::Unknown: break;
    }
    return "unknown";
}

const char* auraEventTypeName(AuraEventType t) {
    switch (t) {
        case AuraEventType::Applied:     return "applied";
        case AuraEventType::Removed:     return "removed";
        case AuraEventType::Refresh:     return "refresh";
        case AuraEventType::AppliedDose: return "applied_dose";
        case AuraEventType::RemovedDose: return "removed_dose";
        case AuraEventType::Broken:      return "broken";
        case AuraEventType::BrokenSpell: return "broken_spell";
    }
    return "unknown";
}

const char* dispelEventTypeName(DispelInterruptEvent::EventType t) {
    switch (t) {
        case DispelInterruptEvent::EventType::Dispel:       return "dispel";
        case DispelInterruptEvent::EventType::Interrupt:    return "interrupt";
        case DispelInterruptEvent::EventType::Stolen:       return "stolen";
        case DispelInterruptEvent::EventType::DispelFailed: return "dispel_failed";
    }
    return "unknown";
}

// CombatEvent and CooldownReductionRecord hold interned ids rather than strings.
// The package format calls for full GUIDs, so they get resolved on the way out.
std::string_view resolveGuid(uint32_t id) {
    if (id == 0) return {};
    return guidInterner().lookup(id);
}

// Prefers what the caller resolved, then whatever the log itself carried.
// WCL events have no inline names, so without the first lookup those columns
// come out as bare ids; local logs populate the second.
std::string_view spellName(const ExtractedData& data, const SpellNames* names,
                           uint32_t spellId);

// Some events carry the name inline, which local logs fill and WCL leaves
// empty. Falling back to the resolved map keeps those columns reading the
// same whichever source the fight came from.
std::string_view inlineOr(std::string_view inlineName, const ExtractedData& data,
                          const SpellNames* names, uint32_t spellId) {
    if (!inlineName.empty()) return inlineName;
    return spellName(data, names, spellId);
}

std::string_view spellName(const ExtractedData& data, const SpellNames* names,
                           uint32_t spellId) {
    if (names) {
        auto it = names->find(spellId);
        if (it != names->end() && !it->second.empty()) return it->second;
    }
    auto it = data.spellNameMap.find(spellId);
    return it == data.spellNameMap.end() ? std::string_view{} : std::string_view(it->second);
}

// Which actors count as "the player" for scoping: the player plus anything they
// own. Built once per export rather than per row.
std::unordered_set<std::string> playerAndPets(const ExtractedData& data,
                                              std::string_view playerGuid) {
    std::unordered_set<std::string> owned;
    if (playerGuid.empty()) return owned;
    owned.emplace(playerGuid);

    // Summons name the summoner directly
    for (const auto& s : data.summonEvents) {
        if (s.summoner_guid == playerGuid) owned.emplace(s.summoned_guid);
    }
    // Combat events carry an owner id for pet actions, which catches pets that
    // were summoned before the pull started and so have no summon event.
    for (const auto& c : data.combatEvents) {
        if (c.owner_guid_id == 0) continue;
        if (resolveGuid(c.owner_guid_id) != playerGuid) continue;
        auto pet = resolveGuid(c.source_guid_id);
        if (!pet.empty()) owned.emplace(pet);
    }
    return owned;
}

bool boolField(bool v) { return v; }

} // namespace

bool writeCastsCsv(const std::string& path,
                   const ExtractedData& data,
                   std::string_view playerGuid,
                   const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    // cooldown_ms and cooldown_ready_ms are written as they are recorded, which
    // is zero: SpellGCDDatabase's data was removed for being unreliable and
    // nothing has replaced it. The columns stay so the schema does not shift
    // under a reader if a source appears, but nothing should be concluded from
    // them - a check comparing cast gaps against cooldown_ms silently compares
    // against zero and finds nothing. Measure from the casts instead.
    out << "start_ms,end_ms,actor_guid,target_guid,spell_id,spell_name,"
           "cast_duration_ms,haste_permille,power_cost,gcd_ms,cooldown_ms,"
           "cooldown_ready_ms,cancelled\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.spellCastEvents) {
        // Casts are about what the player did, so pets count but nothing else
        if (scoped && owned.find(ev.actor_guid) == owned.end()) continue;

        out << ev.start_timestamp_ms << ','
            << ev.end_timestamp_ms << ','
            << csvField(ev.actor_guid) << ','
            << csvField(ev.target_guid) << ','
            << ev.spell_id << ','
            << csvField(spellName(data, names, ev.spell_id)) << ','
            << ev.cast_duration_ms() << ','
            << ev.haste_permille << ','
            << ev.power_cost << ','
            << ev.gcd_ms << ','
            << ev.cooldown_ms << ','
            << ev.cooldown_ready_ms << ','
            << (boolField(ev.cancelled) ? 1 : 0) << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeCombatCsv(const std::string& path,
                    const ExtractedData& data,
                    std::string_view playerGuid,
                    const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,event_type,direction,source_guid,target_guid,owner_guid,"
           "spell_id,spell_name,spell_school,amount,effective_amount,absorbed,"
           "blocked,resisted,critical,periodic,overkill,overheal,swing\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.combatEvents) {
        const auto source = resolveGuid(ev.source_guid_id);
        const auto target = resolveGuid(ev.target_guid_id);

        const bool byPlayer = !source.empty() && owned.find(std::string(source)) != owned.end();
        const bool atPlayer = !target.empty() && owned.find(std::string(target)) != owned.end();

        // Unlike casts, this table also has to keep what hit the player
        if (scoped && !byPlayer && !atPlayer) continue;

        const char* direction = byPlayer ? (atPlayer ? "self" : "outgoing") : "incoming";

        auto flag = [&](uint16_t bit) { return (ev.flags & bit) ? 1 : 0; };

        out << ev.timestamp_ms << ','
            << combatEventTypeName(ev.event_type) << ','
            << direction << ','
            << csvField(source) << ','
            << csvField(target) << ','
            << csvField(resolveGuid(ev.owner_guid_id)) << ','
            << ev.spell_id << ','
            << csvField(spellName(data, names, ev.spell_id)) << ','
            << static_cast<int>(ev.spell_school) << ','
            << ev.amount << ','
            << ev.effective_amount << ','
            << ev.absorbed << ','
            << ev.blocked << ','
            << ev.resisted << ','
            << flag(CombatEventFlags::Critical) << ','
            << flag(CombatEventFlags::Periodic) << ','
            << flag(CombatEventFlags::Overkill) << ','
            << flag(CombatEventFlags::Overheal) << ','
            << flag(CombatEventFlags::IsSwing) << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeAurasCsv(const std::string& path,
                   const ExtractedData& data,
                   std::string_view playerGuid,
                   const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,event_type,aura_type,target_guid,source_guid,"
           "spell_id,spell_name,stacks,breaking_spell_id,breaking_spell_name\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.auraEvents) {
        const bool onPlayer = owned.find(ev.target_guid) != owned.end();
        const bool byPlayer = owned.find(ev.source_guid) != owned.end();

        // Buffs the player has, and debuffs the player put on something else
        if (scoped && !onPlayer && !byPlayer) continue;

        out << ev.timestamp_ms << ','
            << auraEventTypeName(ev.event_type) << ','
            << auraTypeName(ev.aura_type) << ','
            << csvField(ev.target_guid) << ','
            << csvField(ev.source_guid) << ','
            << ev.spell_id << ','
            << csvField(inlineOr(ev.spell_name, data, names, ev.spell_id)) << ','
            << static_cast<int>(ev.stacks) << ','
            << ev.breaking_spell_id << ','
            << csvField(inlineOr(ev.breaking_spell_name, data, names, ev.breaking_spell_id)) << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writePositionsCsv(const std::string& path,
                       const ExtractedData& data,
                       std::string_view playerGuid) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,actor_guid,actor_name,actor_role,x,y,map_id,facing_radians\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.actorEvents) {
        const std::string guid(ev.source_guid);
        const bool isOwned = owned.find(guid) != owned.end();
        const bool isHostile = data.hostileGuids.find(guid) != data.hostileGuids.end();

        // Enemy movement is kept for context - a boss position is what makes a
        // player position mean anything
        if (scoped && !isOwned && !isHostile) continue;

        const char* role = isOwned ? "player" : (isHostile ? "enemy" : "other");

        out << ev.timestamp_ms << ','
            << csvField(ev.source_guid) << ','
            << csvField(ev.source_actor) << ','
            << role << ','
            << ev.x_loc << ','
            << ev.y_loc << ','
            << ev.map_id << ','
            << ev.facing_radians << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeDeathsCsv(const std::string& path,
                    const ExtractedData& data,
                    std::string_view playerGuid,
                    const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,event_type,actor_guid,actor_name,spell_id,spell_name\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.deathEvents) {
        if (scoped && owned.find(ev.actor_guid) == owned.end()) continue;
        out << ev.timestamp_ms << ",death,"
            << csvField(ev.actor_guid) << ','
            << csvField(ev.actor_name) << ','
            << ev.killing_spell_id << ','
            << csvField(inlineOr(ev.killing_spell_name, data, names, ev.killing_spell_id)) << "\r\n";
    }

    for (const auto& ev : data.resurrectEvents) {
        if (scoped && owned.find(ev.actor_guid) == owned.end()) continue;
        out << ev.timestamp_ms << ",resurrect,"
            << csvField(ev.actor_guid) << ','
            << csvField(ev.actor_name) << ','
            << ev.spell_id << ','
            << csvField(spellName(data, names, ev.spell_id)) << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeDefenseCsv(const std::string& path,
                     const ExtractedData& data,
                     std::string_view playerGuid,
                     const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,event_type,detail,source_guid,target_guid,absorber_guid,"
           "spell_id,spell_name,shield_spell_id,shield_spell_name,amount,"
           "total_damage,critical,periodic\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.missedEvents) {
        // What failed to land on the player
        if (scoped && owned.find(ev.target_guid) == owned.end()) continue;

        // One statement per column, in header order, so the blanks stay
        // countable - a run like ",," is too easy to get off by one
        out << ev.timestamp_ms << ','              // timestamp_ms
            << "missed" << ','                     // event_type
            << missTypeName(ev.miss_type) << ','   // detail
            << csvField(ev.source_guid) << ','     // source_guid
            << csvField(ev.target_guid) << ','     // target_guid
            << ','                                 // absorber_guid - n/a
            << ev.spell_id << ','                  // spell_id
            << csvField(inlineOr(ev.spell_name, data, names, ev.spell_id)) << ','      // spell_name
            << ','                                 // shield_spell_id - n/a
            << ','                                 // shield_spell_name - n/a
            << ev.amount_missed << ','             // amount
            << ','                                 // total_damage - not reported
            << (ev.is_critical ? 1 : 0) << ','     // critical
            << (ev.is_periodic ? 1 : 0)            // periodic
            << "\r\n";
    }

    for (const auto& ev : data.absorbEvents) {
        if (scoped && owned.find(ev.target_guid) == owned.end()) continue;

        out << ev.timestamp_ms << ','                 // timestamp_ms
            << "absorbed" << ','                      // event_type
            << ','                                    // detail - misses only
            << csvField(ev.source_guid) << ','        // source_guid
            << csvField(ev.target_guid) << ','        // target_guid
            << csvField(ev.absorber_guid) << ','      // absorber_guid
            << ev.source_spell_id << ','              // spell_id
            << csvField(inlineOr(ev.source_spell_name, data, names, ev.source_spell_id)) << ','  // spell_name
            << ev.absorb_spell_id << ','              // shield_spell_id
            << csvField(inlineOr(ev.absorb_spell_name, data, names, ev.absorb_spell_id)) << ','  // shield_spell_name
            << ev.absorbed_amount << ','              // amount
            << ev.total_damage << ','                 // total_damage
            << ','                                    // critical - not tracked
                                                      // periodic - not tracked
            << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeUtilityCsv(const std::string& path,
                     const ExtractedData& data,
                     std::string_view playerGuid,
                     const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,event_type,source_guid,target_guid,spell_id,spell_name,"
           "target_spell_id,target_spell_name,target_spell_school\r\n";

    const auto owned = playerAndPets(data, playerGuid);
    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.dispelInterruptEvents) {
        // Interrupts and dispels the player performed
        if (scoped && owned.find(ev.source_guid) == owned.end()) continue;

        out << ev.timestamp_ms << ','
            << dispelEventTypeName(ev.event_type) << ','
            << csvField(ev.source_guid) << ','
            << csvField(ev.target_guid) << ','
            << ev.spell_id << ','
            << csvField(inlineOr(ev.spell_name, data, names, ev.spell_id)) << ','
            << ev.extra_spell_id << ','
            << csvField(inlineOr(ev.extra_spell_name, data, names, ev.extra_spell_id)) << ','
            << ev.extra_spell_school << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeSummonsCsv(const std::string& path,
                     const ExtractedData& data,
                     std::string_view playerGuid,
                     const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,summoner_guid,summoned_guid,spell_id,spell_name\r\n";

    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.summonEvents) {
        if (scoped && ev.summoner_guid != playerGuid) continue;

        out << ev.timestamp_ms << ','
            << csvField(ev.summoner_guid) << ','
            << csvField(ev.summoned_guid) << ','
            << ev.spell_id << ','
            << csvField(spellName(data, names, ev.spell_id)) << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeCdReductionsCsv(const std::string& path,
                          const ExtractedData& data,
                          std::string_view playerGuid,
                          const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,actor_guid,spell_id,spell_name,reduction_ms\r\n";

    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.cooldownReductions) {
        const auto actor = resolveGuid(ev.actor_guid_id);
        if (scoped && actor != playerGuid) continue;

        out << ev.timestamp_ms << ','
            << csvField(actor) << ','
            << ev.spell_id << ','
            << csvField(spellName(data, names, ev.spell_id)) << ','
            << ev.reduction_ms << "\r\n";
    }

    out.flush();
    return out.good();
}

bool writeCombatantJson(const std::string& path,
                        const ExtractedData& data,
                        std::string_view playerGuid) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "{\n  \"combatants\": [\n";

    bool first = true;
    for (const auto& ev : data.combatantInfoEvents) {
        if (!playerGuid.empty() && ev.player_guid != playerGuid) continue;

        if (!first) out << ",\n";
        first = false;

        auto nameIt = data.guidToNameMap.find(ev.player_guid);
        const std::string_view name =
            nameIt == data.guidToNameMap.end() ? std::string_view{}
                                               : std::string_view(nameIt->second);

        out << "    {\n"
            << "      \"player_guid\": " << jsonString(ev.player_guid) << ",\n"
            << "      \"player_name\": " << jsonString(name) << ",\n"
            << "      \"spec_id\": " << ev.spec_id << ",\n"
            << "      \"haste_melee\": " << ev.haste_melee << ",\n"
            << "      \"haste_ranged\": " << ev.haste_ranged << ",\n"
            << "      \"haste_spell\": " << ev.haste_spell << ",\n"
            << "      \"item_level\": ";
        if (ev.item_level != 0) out << ev.item_level; else out << "null";
        out << ",\n      \"gear\": [";
        for (size_t g = 0; g < ev.gear.size(); ++g) {
            if (g) out << ", ";
            out << "{\"item_id\": " << ev.gear[g].item_id
                << ", \"item_level\": " << ev.gear[g].item_level << "}";
        }
        out << "],\n"
            << "      \"talents\": [";

        for (size_t i = 0; i < ev.talents.size(); ++i) {
            if (i) out << ", ";
            out << "{\"spell_id\": " << ev.talents[i].spell_id
                << ", \"rank\": " << static_cast<int>(ev.talents[i].rank) << "}";
        }

        out << "]\n    }";
    }

    out << "\n  ]\n}\n";
    out.flush();
    return out.good();
}

bool writeLookupsJson(const std::string& path, const ExtractedData& data,
                      const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    // Union of what the log named and what the caller resolved, so a WCL
    // package still explains every id its tables mention
    SpellNames merged;
    for (const auto& [id, name] : data.spellNameMap) merged[id] = name;
    if (names) {
        for (const auto& [id, name] : *names) {
            if (!name.empty()) merged[id] = name;
        }
    }

    out << "{\n  \"spells\": {\n";
    bool first = true;
    for (const auto& [id, name] : merged) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << id << "\": " << jsonString(name);
    }
    out << "\n  },\n  \"actors\": {\n";

    first = true;
    for (const auto& [guid, name] : data.guidToNameMap) {
        if (!first) out << ",\n";
        first = false;
        out << "    " << jsonString(guid) << ": " << jsonString(name);
    }
    out << "\n  }\n}\n";

    out.flush();
    return out.good();
}

} // namespace awow::analysis
