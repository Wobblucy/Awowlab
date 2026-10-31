#include "../OutputWriter.h"
#include "Core/StringInterner.h"
#include <iostream>
#include <algorithm>

std::unordered_map<StringInterner::Id, StringInterner::Id>
OutputWriter::buildSummonPetToOwnerMap(const ExtractedData& data) {
    // Build pet-to-owner map from SPELL_SUMMON events (authoritative source)
    // Keyed by interned guid id; this runs on the parse path so intern() is safe.
    std::unordered_map<StringInterner::Id, StringInterner::Id> petToOwnerFromSummons;
    for (const auto& summon : data.summonEvents) {
        // Only track if summoner is a Player (we want to attribute to players)
        if (summon.summoner_guid.starts_with("Player-")) {
            petToOwnerFromSummons[guidInterner().intern(summon.summoned_guid)] =
                guidInterner().intern(summon.summoner_guid);
        }
    }
    return petToOwnerFromSummons;
}

ActorMap OutputWriter::buildActorMap(const ExtractedData& data) {
    ActorMap actorMap;

    auto petToOwnerFromSummons = buildSummonPetToOwnerMap(data);

    // Populate resource_status_table from resourceEvents
    for (const auto& resEvent : data.resourceEvents) {
        ResourceStatusRecord record{};
        record.timestamp_ms = resEvent.timestamp_ms;
        record.current_health = resEvent.current_health;
        record.max_health = resEvent.max_health;
        record.current_power = resEvent.current_power;
        record.max_power = resEvent.max_power;
        record.powertype = resEvent.power_type;

        actorMap[guidInterner().intern(resEvent.actor_guid)].resource_status_table.push_back(record);
    }

    // Populate spell_cast_table from spellCastEvents
    for (const auto& castEvent : data.spellCastEvents) {
        SpellCastRecord record{};
        record.timestamp_ms = castEvent.end_timestamp_ms;  // Use completion time
        record.start_timestamp_ms = castEvent.start_timestamp_ms;
        record.spell_id = castEvent.spell_id;
        record.haste_permille = castEvent.haste_permille;
        record.cancelled = castEvent.cancelled ? 1 : 0;

        actorMap[guidInterner().intern(castEvent.actor_guid)].spell_cast_table.push_back(record);
    }

    // Populate damage_dealt_table and healing_done_table from combatEvents
    for (const auto& combatEvent : data.combatEvents) {
        CombatRecord record{};
        record.timestamp_ms = combatEvent.timestamp_ms;
        record.spell_id = combatEvent.spell_id;
        record.amount = combatEvent.amount;
        record.effective_amount = combatEvent.effective_amount;
        record.absorbed = combatEvent.absorbed;
        record.blocked = combatEvent.blocked;
        record.resisted = combatEvent.resisted;
        record.target_guid_id = combatEvent.target_guid_id;
        record.flags = combatEvent.flags;
        record.spell_school = combatEvent.spell_school;
        record.event_type = combatEvent.event_type;

        // Determine owner_guid using priority system:
        // 1. Use existing owner_guid from advanced combat log if valid
        // 2. Fall back to summon event data if combat log data is missing
        std::string_view owner_sv = guidInterner().lookup(combatEvent.owner_guid_id);
        if (!owner_sv.empty() &&
            owner_sv != "0000000000000000" &&
            owner_sv.starts_with("Player-")) {
            // Valid owner from combat log advanced data
            record.owner_guid_id = combatEvent.owner_guid_id;
        } else {
            // Try to find owner from summon events
            auto ownerIt = petToOwnerFromSummons.find(combatEvent.source_guid_id);
            if (ownerIt != petToOwnerFromSummons.end()) {
                record.owner_guid_id = ownerIt->second;
            } else {
                // No owner found, keep whatever was in the combat event
                record.owner_guid_id = combatEvent.owner_guid_id;
            }
        }

        // Route to appropriate table based on event type
        if (combatEvent.event_type == CombatEventType::HealingDone) {
            actorMap[combatEvent.source_guid_id].healing_done_table.push_back(record);
        } else {
            actorMap[combatEvent.source_guid_id].damage_dealt_table.push_back(record);
        }
    }

    // Sort all tables by timestamp for efficient binary search queries
    for (auto& [guid, table] : actorMap) {
        std::sort(table.resource_status_table.begin(), table.resource_status_table.end(),
            [](const ResourceStatusRecord& a, const ResourceStatusRecord& b) {
                return a.timestamp_ms < b.timestamp_ms;
            });
        std::sort(table.spell_cast_table.begin(), table.spell_cast_table.end(),
            [](const SpellCastRecord& a, const SpellCastRecord& b) {
                return a.timestamp_ms < b.timestamp_ms;
            });
        std::sort(table.damage_dealt_table.begin(), table.damage_dealt_table.end(),
            [](const CombatRecord& a, const CombatRecord& b) {
                return a.timestamp_ms < b.timestamp_ms;
            });
        std::sort(table.healing_done_table.begin(), table.healing_done_table.end(),
            [](const CombatRecord& a, const CombatRecord& b) {
                return a.timestamp_ms < b.timestamp_ms;
            });
    }

    return actorMap;
}
