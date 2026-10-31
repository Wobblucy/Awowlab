#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include "EventTypes.h"
#include "Actor/ActorMap.h"
#include "Core/StringInterner.h"

class OutputWriter {
public:
    // Build the authoritative pet -> owner lineage from SPELL_SUMMON events,
    // keyed by interned guid id (summoned unit id -> summoner id). Only
    // player-summoned units are kept - we attribute pet damage to players.
    // Shared by buildActorMap (to stamp owner_guid on records) and by the
    // caller (to hand the same lineage to CombatDatabase::loadFromActorMap).
    static std::unordered_map<StringInterner::Id, StringInterner::Id>
    buildSummonPetToOwnerMap(const ExtractedData& data);

    // Extracts every event type in one pass over the lines
    static ExtractedData extractAllEvents(
        const std::vector<std::vector<std::string_view>>& all_lines
    );

    // Writes position events to CSV (debug builds call this after each load)
    static bool writeToCSV(
        const std::vector<ActorEvent>& events,
        const std::string& filename
    );

    // Build ActorMap with per-actor tables (resource_status_table, spell_cast_table)
    // populated from extracted data. Tables are sorted by timestamp for binary search.
    static ActorMap buildActorMap(const ExtractedData& data);
};
