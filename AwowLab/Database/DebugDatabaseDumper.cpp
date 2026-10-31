// Only compile debug database dumper in debug builds
#ifndef NDEBUG

#include "DebugDatabaseDumper.h"
#include "Core/StringInterner.h"
#include "Core/UnifiedSettings.h"
#include "EventDatabase.h"
#include "SpellCastDatabase.h"
#include "ResourceDatabase.h"
#include "CombatDatabase.h"
#include "DeathDatabase.h"
#include "AuraDatabase.h"
#include "OutputWriter.h"
#include "Actor/ActorMap.h"
#include "Actor/ActorTable.h"
#include "AutoAnnotationTriggerManager.h"
#include "AutoAnnotationTypes.h"
#include "Parser/WowClass.h"
#include "Parser/Include/Structures/CombatTypes.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <charconv>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>

// Local spell timing data for debug output (avoids UIManager dependency)
namespace {

struct LocalSpellTimingData {
    uint32_t gcd_ms = 0;
    uint32_t cooldown_ms = 0;
    uint32_t cast_time_ms = 0;
};

static std::unordered_map<uint32_t, LocalSpellTimingData> s_spellTimings;
static bool s_spellGcdDbLoaded = false;
static constexpr uint32_t DEFAULT_GCD_MS = 1500;

// Binary format matches SpellDataTable.cpp
#pragma pack(push, 1)
struct SpellDataRecord {
    uint32_t spell_id;
    int32_t  gcd_ms;
    int32_t  cooldown_ms;
    int32_t  cast_time_ms;
    uint8_t  gcd_haste_affected;
    uint8_t  cd_haste_affected;
    uint8_t  padding[2];
};
#pragma pack(pop)

bool loadSpellDataFromBinary(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    s_spellTimings.clear();

    // Read header
    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    if (!file) {
        return false;
    }

    // Read all records
    for (uint32_t i = 0; i < count; ++i) {
        SpellDataRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(SpellDataRecord));
        if (!file) {
            break;
        }

        s_spellTimings[record.spell_id] = LocalSpellTimingData{
            static_cast<uint32_t>(record.gcd_ms),
            static_cast<uint32_t>(record.cooldown_ms),
            static_cast<uint32_t>(record.cast_time_ms)
        };
    }

    return !s_spellTimings.empty();
}

LocalSpellTimingData getSpellTiming(uint32_t spellId) {
    auto it = s_spellTimings.find(spellId);
    if (it != s_spellTimings.end()) {
        return it->second;
    }
    return LocalSpellTimingData{DEFAULT_GCD_MS, 0, 0};
}

void ensureSpellGCDDatabaseLoaded() {
    if (!s_spellGcdDbLoaded) {
        std::filesystem::path exePath = std::filesystem::current_path();
        std::string spellDataVersion = SettingsCache::instance().get().spellDataVersion;
        std::string binPath = (exePath / "data" / ("spell_data_" + spellDataVersion + ".bin")).string();
        if (loadSpellDataFromBinary(binPath)) {
            std::cout << "[DebugDatabaseDumper] Loaded " << s_spellTimings.size() << " spells from " << binPath << "\n";
        } else {
            std::cout << "[DebugDatabaseDumper] Could not load spell data from " << binPath << "\n";
        }
        s_spellGcdDbLoaded = true;
    }
}

} // anonymous namespace

// Calculate expected GCD with haste and the GCD floor: 0.5s for a base GCD
// of 1.0s or less, 0.75s for anything longer
static uint32_t calculateExpectedGCD(uint32_t baseGcdMs, float hastePercent) {
    if (baseGcdMs == 0) return 0;  // Off-GCD ability

    // Apply haste: actualGcd = baseGcd / (1 + hastePercent/100)
    float hasteMultiplier = 1.0f + (hastePercent / 100.0f);
    float actualGcdMs = static_cast<float>(baseGcdMs) / hasteMultiplier;

    const uint32_t floorMs = baseGcdMs <= 1000 ? 500 : 750;
    return std::max(static_cast<uint32_t>(actualGcdMs), floorMs);
}

// Calculate expected cast time with haste
static uint32_t calculateExpectedCastTime(uint32_t baseCastTimeMs, float hastePercent) {
    if (baseCastTimeMs == 0) return 0;  // Instant cast

    // Apply haste: actualCast = baseCast / (1 + hastePercent/100)
    float hasteMultiplier = 1.0f + (hastePercent / 100.0f);
    float actualCastMs = static_cast<float>(baseCastTimeMs) / hasteMultiplier;

    return static_cast<uint32_t>(actualCastMs);
}

std::string DebugDatabaseDumper::escapeCSV(std::string_view field) {
    bool needsQuotes = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuotes = true;
            break;
        }
    }

    if (!needsQuotes) {
        return std::string(field);
    }

    std::string result = "\"";
    for (char c : field) {
        if (c == '"') {
            result += "\"\"";
        } else {
            result += c;
        }
    }
    result += "\"";
    return result;
}

bool DebugDatabaseDumper::ensureDirectory(const std::string& path) {
    try {
        std::filesystem::create_directories(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create directory " << path << ": " << e.what() << "\n";
        return false;
    }
}

std::string DebugDatabaseDumper::formatTimestamp(int32_t timestamp_ms) {
    // Handle negative timestamps for pre-encounter events
    bool negative = timestamp_ms < 0;
    uint32_t abs_ts = negative ? static_cast<uint32_t>(-timestamp_ms) : static_cast<uint32_t>(timestamp_ms);
    uint32_t minutes = abs_ts / 60000;
    uint32_t seconds = (abs_ts % 60000) / 1000;
    uint32_t millis = abs_ts % 1000;

    std::ostringstream oss;
    if (negative) {
        oss << "-";
    }
    oss << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds << "."
        << std::setfill('0') << std::setw(3) << millis;
    return oss.str();
}

bool DebugDatabaseDumper::dumpAll(
    const std::string& outputDir,
    const std::string& sourcePrefix,
    const EventDatabase* eventDb,
    const SpellCastDatabase* spellCastDb,
    [[maybe_unused]] const ResourceDatabase* resourceDb,
    const CombatDatabase* combatDb,
    const DeathDatabase* deathDb,
    const AuraDatabase* auraDb,
    const ActorMap* actorMap,
    const std::vector<CombatantInfoEvent>* combatantInfo,
    const std::vector<MapChangeEvent>* mapChanges,
    const std::vector<SummonEvent>* summons,
    const std::vector<ResurrectEvent>* resurrects,
    const AutoAnnotationTriggerManager* annotationTriggers,
    const std::unordered_map<uint32_t, std::string>* spellNameMap,
    const std::unordered_map<std::string, std::string>* guidToNameMap,
    const std::vector<AuraEvent>* auraEventsRaw,
    const ExtractedData* extractedData
) {
    std::string debugDir = outputDir + "/debuglogs";

    if (!ensureDirectory(debugDir)) {
        return false;
    }

    bool allSuccess = true;

    std::cout << "=== Dumping databases to CSV (" << sourcePrefix << ") ===\n";
    std::cout << "Output directory: " << debugDir << "\n\n";

    // Writes one CSV when there is data for it and reports the outcome
    auto dumpIf = [&](bool hasData, const char* fileName, const char* label,
                      const auto& dump) {
        if (!hasData) return;
        std::string path = debugDir + "/" + sourcePrefix + fileName;
        if (dump(path)) {
            std::cout << "  [OK] " << label << " -> " << path << "\n";
        } else {
            std::cerr << "  [FAIL] " << label << "\n";
            allSuccess = false;
        }
    };

    const bool hasEvents = eventDb && !eventDb->empty();
    const bool hasCasts = spellCastDb && !spellCastDb->empty();
    const bool hasCombat = combatDb && !combatDb->empty();
    const bool hasAuras = auraDb && !auraDb->empty();
    const bool hasActors = actorMap && !actorMap->empty();
    const bool hasTriggers = annotationTriggers && annotationTriggers->hasTriggers();
    const bool hasCachedAnnotations = annotationTriggers && annotationTriggers->getCachedEventCount() > 0;
    const bool hasExtracted = extractedData != nullptr;

    dumpIf(hasEvents, "01_event_database.csv", "EventDatabase",
        [&](const std::string& p) { return dumpEventDatabase(p, eventDb); });
    dumpIf(hasCasts, "02_spell_cast_database.csv", "SpellCastDatabase",
        [&](const std::string& p) { return dumpSpellCastDatabase(p, spellCastDb); });
    dumpIf(combatantInfo && !combatantInfo->empty(), "03_combatant_info.csv", "CombatantInfo",
        [&](const std::string& p) { return dumpCombatantInfo(p, combatantInfo); });
    dumpIf(hasCombat, "04_combat_database.csv", "CombatDatabase",
        [&](const std::string& p) { return dumpCombatDatabase(p, combatDb); });
    dumpIf(deathDb && !deathDb->getAllDeaths().empty(), "05_death_database.csv", "DeathDatabase",
        [&](const std::string& p) { return dumpDeathDatabase(p, deathDb); });
    dumpIf(resurrects && !resurrects->empty(), "05b_resurrects.csv", "Resurrects",
        [&](const std::string& p) { return dumpResurrects(p, resurrects); });
    dumpIf(hasAuras, "06_aura_database.csv", "AuraDatabase",
        [&](const std::string& p) { return dumpAuraDatabase(p, auraDb); });

    dumpIf(hasActors, "08_actormap_spell_casts.csv", "ActorMap spell_cast_table",
        [&](const std::string& p) { return dumpActorMapSpellCasts(p, actorMap); });
    dumpIf(hasActors, "09_actormap_resources.csv", "ActorMap resource_status_table",
        [&](const std::string& p) { return dumpActorMapResources(p, actorMap); });
    dumpIf(hasActors, "10_actormap_damage_dealt.csv", "ActorMap damage_dealt_table",
        [&](const std::string& p) { return dumpActorMapDamageDealt(p, actorMap); });
    dumpIf(hasActors, "11_actormap_healing_done.csv", "ActorMap healing_done_table",
        [&](const std::string& p) { return dumpActorMapHealingDone(p, actorMap); });

    dumpIf(mapChanges && !mapChanges->empty(), "12_map_changes.csv", "MapChanges",
        [&](const std::string& p) { return dumpMapChanges(p, mapChanges); });
    dumpIf(summons && !summons->empty(), "13_summons.csv", "Summons",
        [&](const std::string& p) { return dumpSummons(p, summons); });
    dumpIf(hasTriggers, "14_auto_annotation_triggers.csv", "AutoAnnotationTriggers",
        [&](const std::string& p) { return dumpAutoAnnotationTriggers(p, annotationTriggers); });
    dumpIf(hasCachedAnnotations, "15_cached_annotation_events.csv", "CachedAnnotationEvents",
        [&](const std::string& p) { return dumpCachedAnnotationEvents(p, annotationTriggers); });
    dumpIf(spellNameMap && !spellNameMap->empty(), "16_spell_name_map.csv", "SpellNameMap",
        [&](const std::string& p) { return dumpSpellNameMap(p, spellNameMap); });
    dumpIf(guidToNameMap && !guidToNameMap->empty(), "17_guid_to_name_map.csv", "GuidToNameMap",
        [&](const std::string& p) { return dumpGuidToNameMap(p, guidToNameMap); });
    dumpIf(combatDb && !combatDb->getPetToOwnerMap().empty(), "18_pet_to_owner_map.csv", "PetToOwnerMap",
        [&](const std::string& p) { return dumpPetToOwnerMap(p, combatDb); });
    dumpIf(combatDb && !combatDb->getOwnerToPetsMap().empty(), "19_owner_to_pets_map.csv", "OwnerToPetsMap",
        [&](const std::string& p) { return dumpOwnerToPetsMap(p, combatDb); });
    dumpIf(hasExtracted, "20_spells_seen_by_class.csv", "SpellsSeenByClass",
        [&](const std::string& p) { return dumpSpellsSeenByClass(p, extractedData); });
    dumpIf(hasExtracted, "21_auras_seen_by_class.csv", "AurasSeenByClass",
        [&](const std::string& p) { return dumpAurasSeenByClass(p, extractedData); });
    dumpIf(auraEventsRaw && !auraEventsRaw->empty(), "22_aura_events_raw.csv", "AuraEventsRaw",
        [&](const std::string& p) { return dumpAuraEventsRaw(p, auraEventsRaw); });
    dumpIf(hasAuras, "23_aura_source_index.csv", "AuraSourceIndex",
        [&](const std::string& p) { return dumpAuraSourceIndex(p, auraDb); });
    dumpIf(hasCombat, "24_damage_taken_by_target.csv", "DamageTakenIndex",
        [&](const std::string& p) { return dumpDamageTakenIndex(p, combatDb); });
    dumpIf(hasCombat, "25_healing_received_by_target.csv", "HealingReceivedIndex",
        [&](const std::string& p) { return dumpHealingReceivedIndex(p, combatDb); });
    dumpIf(hasExtracted && !extractedData->missedEvents.empty(), "26_missed_events.csv", "MissedEvents",
        [&](const std::string& p) { return dumpMissedEvents(p, &extractedData->missedEvents); });
    dumpIf(hasExtracted && !extractedData->absorbEvents.empty(), "27_absorb_events.csv", "AbsorbEvents",
        [&](const std::string& p) { return dumpAbsorbEvents(p, &extractedData->absorbEvents); });

    std::cout << "\n=== Database dump complete ===\n";
    return allSuccess;
}

// ActorEvent struct order:
// source_guid, source_actor, actor_type, flags (raw + parsed), timestamp_ms, x_loc, y_loc, map_id
bool DebugDatabaseDumper::dumpEventDatabase(const std::string& filepath, const EventDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "source_guid,source_actor,actor_type,flags_raw,flags_affiliation,flags_reaction,"
         << "flags_controller,flags_unit_type,flags_raid_marker,"
         << "timestamp_ms,x_loc,y_loc,map_id\n";

    db->forEachEvent([&file](const ActorEvent& e) {
        file << escapeCSV(e.source_guid) << ","
             << escapeCSV(e.source_actor) << ","
             << static_cast<int>(e.actor_type) << ","
             << "0x" << std::hex << e.flags.raw << std::dec << ","
             << static_cast<int>(e.flags.affiliation) << ","
             << static_cast<int>(e.flags.reaction) << ","
             << static_cast<int>(e.flags.controller) << ","
             << static_cast<int>(e.flags.unitType) << ","
             << static_cast<int>(e.flags.raidMarker) << ","
             << e.timestamp_ms << ","
             << std::fixed << std::setprecision(2) << e.x_loc << ","
             << std::fixed << std::setprecision(2) << e.y_loc << ","
             << e.map_id << "\n";
    });

    return true;
}

bool DebugDatabaseDumper::dumpSpellCastDatabase(const std::string& filepath, const SpellCastDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    // SpellCastDatabase doesn't expose direct iteration - note this limitation
    file << "# SpellCastDatabase doesn't expose actor iteration API.\n";
    file << "# Use ActorMap spell_cast_table (08_actormap_spell_casts.csv) for full data.\n";
    file << "# Time range: " << db->getMinTimestamp() << " - " << db->getMaxTimestamp() << " ms\n";

    auto spellIds = db->getUniqueSpellIDs();
    file << "# Unique spell IDs: " << spellIds.size() << "\n";

    return true;
}

bool DebugDatabaseDumper::dumpCombatDatabase(const std::string& filepath, const CombatDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    uint32_t minTime = db->getMinTimestamp();
    uint32_t maxTime = db->getMaxTimestamp();
    float duration = (maxTime - minTime) / 1000.0f;

    file << "# CombatDatabase Summary\n";
    file << "# Time range: " << minTime << " - " << maxTime << " ms (" << duration << " seconds)\n";
    file << "# Use ActorMap tables (10/11) for raw per-event data.\n\n";

    constexpr size_t RANKING_LIMIT = 100;

    auto writeRanking = [&file](const char* heading, const auto& stats) {
        file << "# ===== " << heading << " (Pets Merged) =====\n";
        file << "actor_guid,total_amount,effective_amount,hit_count,crit_count,amount_per_second,percent_of_total\n";
        for (const auto& stat : stats) {
            file << escapeCSV(stat.actor_guid) << ","
                 << stat.total_amount << ","
                 << stat.effective_amount << ","
                 << stat.hit_count << ","
                 << stat.crit_count << ","
                 << std::fixed << std::setprecision(1) << stat.amount_per_second << ","
                 << std::fixed << std::setprecision(1) << stat.percent_of_total << "\n";
        }
    };

    writeRanking("DAMAGE DEALT",
        db->getRankedByActorWithPets(CombatMetricType::DamageDealt, minTime, maxTime, RANKING_LIMIT));
    file << "\n";
    writeRanking("HEALING DONE",
        db->getRankedByActorWithPets(CombatMetricType::HealingDone, minTime, maxTime, RANKING_LIMIT));
    file << "\n";
    writeRanking("DAMAGE TAKEN",
        db->getRankedByDamageTakenWithPets(minTime, maxTime, RANKING_LIMIT));

    return true;
}

// DeathEvent struct order:
// actor_guid, actor_name, actor_type, timestamp_ms, killing_spell_id, killing_spell_name
bool DebugDatabaseDumper::dumpDeathDatabase(const std::string& filepath, const DeathDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "actor_guid,actor_name,actor_type,timestamp_ms,killing_spell_id,killing_spell_name\n";

    const auto& deaths = db->getAllDeaths();
    for (const auto& death : deaths) {
        file << escapeCSV(death.actor_guid) << ","
             << escapeCSV(death.actor_name) << ","
             << static_cast<int>(death.actor_type) << ","
             << death.timestamp_ms << ","
             << death.killing_spell_id << ","
             << escapeCSV(death.killing_spell_name) << "\n";
    }

    return true;
}

// AuraInstance struct order:
// target_guid, source_guid, spell_id, spell_name, aura_type, applied_at_ms, removed_at_ms, max_stacks, current_stacks, dose_history
bool DebugDatabaseDumper::dumpAuraDatabase(const std::string& filepath, const AuraDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "target_guid,source_guid,spell_id,spell_name,aura_type,applied_at_ms,removed_at_ms,max_stacks,current_stacks,dose_history\n";

    db->forEachAuraInstance([&file](const AuraInstance& instance) {
        file << escapeCSV(instance.target_guid) << ","
             << escapeCSV(instance.source_guid) << ","
             << instance.spell_id << ","
             << escapeCSV(instance.spell_name) << ","
             << static_cast<int>(instance.aura_type) << ","
             << instance.applied_at_ms << ","
             << (instance.removed_at_ms == UINT32_MAX ? -1 : static_cast<int64_t>(instance.removed_at_ms)) << ","
             << static_cast<int>(instance.max_stacks) << ","
             << static_cast<int>(instance.current_stacks) << ",";

        // Format doseHistory as "t1:s1;t2:s2;..."
        for (size_t i = 0; i < instance.doseHistory.size(); ++i) {
            if (i > 0) file << ";";
            file << instance.doseHistory[i].timestamp_ms << ":" << static_cast<int>(instance.doseHistory[i].stack_count);
        }
        file << "\n";
    });

    return true;
}

// SpellCastRecord struct order:
// start_timestamp_ms, timestamp_ms (success), spell_id, haste_permille, cancelled
// Enhanced output includes expected_cast_time_ms and expected_gcd_ms
bool DebugDatabaseDumper::dumpActorMapSpellCasts(const std::string& filepath, const ActorMap* actorMap) {
    if (!actorMap) return false;

    // Ensure spell GCD database is loaded for timing calculations
    ensureSpellGCDDatabaseLoaded();

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "actor_guid,start_timestamp_ms,success_timestamp_ms,spell_id,haste_percent,cancelled,"
         << "expected_cast_time_ms,expected_gcd_ms,base_cast_time_ms,base_gcd_ms\n";

    for (const auto& [guid, table] : *actorMap) {
        for (const auto& record : table.spell_cast_table) {
            // Skip spell_id 1 (melee swing) - WarcraftLogs uses this for auto-attacks
            if (record.spell_id == 1) {
                continue;
            }

            float hastePercent = record.haste_percent();

            // Get base timing data from spell GCD database
            LocalSpellTimingData timing = getSpellTiming(record.spell_id);

            // Calculate expected cast time and GCD with haste
            uint32_t expectedCastMs = calculateExpectedCastTime(timing.cast_time_ms, hastePercent);
            uint32_t expectedGcdMs = calculateExpectedGCD(timing.gcd_ms, hastePercent);

            // Start timestamp (same as success if instant)
            uint32_t startTs = record.start_timestamp_ms;
            if (startTs == 0) {
                startTs = record.timestamp_ms;  // Instant cast - start = end
            }

            file << escapeCSV(guidInterner().lookup(guid)) << ","
                 << startTs << ",";

            // Success timestamp (nil if cancelled)
            if (record.cancelled) {
                file << "nil";
            } else {
                file << record.timestamp_ms;
            }

            file << ","
                 << record.spell_id << ","
                 << std::fixed << std::setprecision(1) << hastePercent << ","
                 << (record.cancelled ? "true" : "false") << ","
                 << expectedCastMs << ","
                 << expectedGcdMs << ","
                 << timing.cast_time_ms << ","
                 << timing.gcd_ms << "\n";
        }
    }

    return true;
}

// ResourceStatusRecord struct order:
// timestamp_ms, current_health, max_health, current_power, max_power, powertype
bool DebugDatabaseDumper::dumpActorMapResources(const std::string& filepath, const ActorMap* actorMap) {
    if (!actorMap) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "actor_guid,timestamp_ms,current_health,max_health,current_power,max_power,powertype\n";

    for (const auto& [guid, table] : *actorMap) {
        for (const auto& record : table.resource_status_table) {
            file << escapeCSV(guidInterner().lookup(guid)) << ","
                 << record.timestamp_ms << ","
                 << record.current_health << ","
                 << record.max_health << ","
                 << record.current_power << ","
                 << record.max_power << ","
                 << static_cast<int>(record.powertype) << "\n";
        }
    }

    return true;
}

// CombatRecord struct order:
// timestamp_ms, spell_id, amount, effective_amount, absorbed, blocked, resisted,
// target_guid_id, owner_guid_id, flags, spell_school, event_type
// Plus extracted flags: is_critical, is_periodic, is_offhand, is_glancing, is_crushing
bool DebugDatabaseDumper::dumpActorMapDamageDealt(const std::string& filepath, const ActorMap* actorMap) {
    if (!actorMap) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "source_guid,timestamp_ms,spell_id,amount,effective_amount,absorbed,blocked,resisted,"
         << "target_guid,owner_guid,flags,spell_school,event_type,"
         << "is_critical,is_periodic,is_offhand,is_glancing,is_crushing,is_swing\n";

    for (const auto& [guid, table] : *actorMap) {
        for (const auto& record : table.damage_dealt_table) {
            // Extract flag bits
            bool is_critical = (record.flags & CombatEventFlags::Critical) != 0;
            bool is_periodic = (record.flags & CombatEventFlags::Periodic) != 0;
            bool is_offhand = (record.flags & CombatEventFlags::OffHand) != 0;
            bool is_glancing = (record.flags & CombatEventFlags::Glancing) != 0;
            bool is_crushing = (record.flags & CombatEventFlags::Crushing) != 0;
            bool is_swing = (record.flags & CombatEventFlags::IsSwing) != 0;

            file << escapeCSV(guidInterner().lookup(guid)) << ","
                 << record.timestamp_ms << ","
                 << record.spell_id << ","
                 << record.amount << ","
                 << record.effective_amount << ","
                 << record.absorbed << ","
                 << record.blocked << ","
                 << record.resisted << ","
                 << escapeCSV(std::string(guidInterner().lookup(record.target_guid_id))) << ","
                 << escapeCSV(std::string(guidInterner().lookup(record.owner_guid_id))) << ","
                 << "0x" << std::hex << record.flags << std::dec << ","
                 << static_cast<int>(record.spell_school) << ","
                 << static_cast<int>(record.event_type) << ","
                 << (is_critical ? "true" : "false") << ","
                 << (is_periodic ? "true" : "false") << ","
                 << (is_offhand ? "true" : "false") << ","
                 << (is_glancing ? "true" : "false") << ","
                 << (is_crushing ? "true" : "false") << ","
                 << (is_swing ? "true" : "false") << "\n";
        }
    }

    return true;
}

// CombatRecord struct order (same as damage):
// timestamp_ms, spell_id, amount, effective_amount, absorbed, blocked, resisted,
// target_guid_id, owner_guid_id, flags, spell_school, event_type
// Plus extracted flags: is_critical, is_periodic, is_overheal
bool DebugDatabaseDumper::dumpActorMapHealingDone(const std::string& filepath, const ActorMap* actorMap) {
    if (!actorMap) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "source_guid,timestamp_ms,spell_id,amount,effective_amount,absorbed,blocked,resisted,"
         << "target_guid,owner_guid,flags,spell_school,event_type,"
         << "is_critical,is_periodic,is_overheal\n";

    for (const auto& [guid, table] : *actorMap) {
        for (const auto& record : table.healing_done_table) {
            // Extract flag bits
            bool is_critical = (record.flags & CombatEventFlags::Critical) != 0;
            bool is_periodic = (record.flags & CombatEventFlags::Periodic) != 0;
            bool is_overheal = (record.flags & CombatEventFlags::Overheal) != 0;

            file << escapeCSV(guidInterner().lookup(guid)) << ","
                 << record.timestamp_ms << ","
                 << record.spell_id << ","
                 << record.amount << ","
                 << record.effective_amount << ","
                 << record.absorbed << ","
                 << record.blocked << ","
                 << record.resisted << ","
                 << escapeCSV(std::string(guidInterner().lookup(record.target_guid_id))) << ","
                 << escapeCSV(std::string(guidInterner().lookup(record.owner_guid_id))) << ","
                 << "0x" << std::hex << record.flags << std::dec << ","
                 << static_cast<int>(record.spell_school) << ","
                 << static_cast<int>(record.event_type) << ","
                 << (is_critical ? "true" : "false") << ","
                 << (is_periodic ? "true" : "false") << ","
                 << (is_overheal ? "true" : "false") << "\n";
        }
    }

    return true;
}

// CombatantInfoEvent struct order:
// player_guid, spec_id
bool DebugDatabaseDumper::dumpCombatantInfo(const std::string& filepath, const std::vector<CombatantInfoEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "player_guid,spec_id\n";

    for (const auto& event : *events) {
        file << escapeCSV(event.player_guid) << ","
             << event.spec_id << "\n";
    }

    return true;
}

// MapChangeEvent struct order:
// timestamp_ms, map_id, map_name, x1, y1, x2, y2
bool DebugDatabaseDumper::dumpMapChanges(const std::string& filepath, const std::vector<MapChangeEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "timestamp_ms,map_id,map_name,x1,y1,x2,y2\n";

    for (const auto& event : *events) {
        file << event.timestamp_ms << ","
             << event.map_id << ","
             << escapeCSV(event.map_name) << ","
             << std::fixed << std::setprecision(2) << event.x1 << ","
             << std::fixed << std::setprecision(2) << event.y1 << ","
             << std::fixed << std::setprecision(2) << event.x2 << ","
             << std::fixed << std::setprecision(2) << event.y2 << "\n";
    }

    return true;
}

// SummonEvent struct order:
// summoner_guid, summoned_guid, timestamp_ms, spell_id
bool DebugDatabaseDumper::dumpSummons(const std::string& filepath, const std::vector<SummonEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "summoner_guid,summoned_guid,timestamp_ms,spell_id\n";

    for (const auto& event : *events) {
        file << escapeCSV(event.summoner_guid) << ","
             << escapeCSV(event.summoned_guid) << ","
             << event.timestamp_ms << ","
             << event.spell_id << "\n";
    }

    return true;
}

// ResurrectEvent struct order:
// actor_guid, actor_name, timestamp_ms, spell_id
bool DebugDatabaseDumper::dumpResurrects(const std::string& filepath, const std::vector<ResurrectEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "actor_guid,actor_name,timestamp_ms,spell_id\n";

    for (const auto& event : *events) {
        file << escapeCSV(event.actor_guid) << ","
             << escapeCSV(event.actor_name) << ","
             << event.timestamp_ms << ","
             << event.spell_id << "\n";
    }

    return true;
}

// Helper to convert TriggerTiming enum to string
static const char* triggerTimingStr(TriggerTiming timing) {
    switch (timing) {
        case TriggerTiming::OnCastSuccess: return "OnCastSuccess";
        case TriggerTiming::OnDamage: return "OnDamage";
        case TriggerTiming::OnDebuffApplied: return "OnDebuffApplied";
        case TriggerTiming::DuringDebuff: return "DuringDebuff";
        case TriggerTiming::OnDebuffEnd: return "OnDebuffEnd";
        default: return "Unknown";
    }
}

// Helper to convert TriggerAnchor enum to string
static const char* triggerAnchorStr(TriggerAnchor anchor) {
    switch (anchor) {
        case TriggerAnchor::Caster: return "Caster";
        case TriggerAnchor::Target: return "Target";
        default: return "Unknown";
    }
}

// Helper to convert AnnotationShape enum to string
static const char* annotationShapeStr(AnnotationShape shape) {
    switch (shape) {
        case AnnotationShape::Circle: return "Circle";
        case AnnotationShape::Rectangle: return "Rectangle";
        case AnnotationShape::Cone: return "Cone";
        case AnnotationShape::Line: return "Line";
        default: return "Unknown";
    }
}

// Helper to convert AnnotationDuration enum to string
static const char* annotationDurationStr(AnnotationDuration duration) {
    switch (duration) {
        case AnnotationDuration::Encounter: return "Encounter";
        case AnnotationDuration::Timed: return "Timed";
        default: return "Unknown";
    }
}

// Helper to convert WowClass enum to string
static const char* wowClassStr(parser::WowClass wowClass) {
    switch (wowClass) {
        case parser::WowClass::Warrior: return "Warrior";
        case parser::WowClass::Paladin: return "Paladin";
        case parser::WowClass::Hunter: return "Hunter";
        case parser::WowClass::Rogue: return "Rogue";
        case parser::WowClass::Priest: return "Priest";
        case parser::WowClass::DeathKnight: return "DeathKnight";
        case parser::WowClass::Shaman: return "Shaman";
        case parser::WowClass::Mage: return "Mage";
        case parser::WowClass::Warlock: return "Warlock";
        case parser::WowClass::Monk: return "Monk";
        case parser::WowClass::Druid: return "Druid";
        case parser::WowClass::DemonHunter: return "DemonHunter";
        case parser::WowClass::Evoker: return "Evoker";
        case parser::WowClass::Unknown: return "Unknown";
        default: return "Unknown";
    }
}

// Helper to convert AuraEventType enum to string
static const char* auraEventTypeStr(AuraEventType type) {
    switch (type) {
        case AuraEventType::Applied: return "Applied";
        case AuraEventType::Removed: return "Removed";
        case AuraEventType::Refresh: return "Refresh";
        case AuraEventType::AppliedDose: return "AppliedDose";
        case AuraEventType::RemovedDose: return "RemovedDose";
        case AuraEventType::Broken: return "Broken";
        case AuraEventType::BrokenSpell: return "BrokenSpell";
        default: return "Unknown";
    }
}

// AutoAnnotationTrigger dump
bool DebugDatabaseDumper::dumpAutoAnnotationTriggers(const std::string& filepath, const AutoAnnotationTriggerManager* manager) {
    if (!manager) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "id,spell_id,creature_guid,creature_name,timing,anchor,shape,"
         << "color_r,color_g,color_b,color_a,dim1,dim2,dim3,dim4,"
         << "duration_type,duration_ms,lead_time_ms,enabled,is_auto_generated\n";

    for (const auto& trigger : manager->getTriggers()) {
        file << trigger.id << ","
             << trigger.spell_id << ","
             << escapeCSV(trigger.creature_guid) << ","
             << escapeCSV(trigger.creature_name) << ","
             << triggerTimingStr(trigger.timing) << ","
             << triggerAnchorStr(trigger.anchor) << ","
             << annotationShapeStr(trigger.shape) << ","
             << std::fixed << std::setprecision(3) << trigger.color[0] << ","
             << std::fixed << std::setprecision(3) << trigger.color[1] << ","
             << std::fixed << std::setprecision(3) << trigger.color[2] << ","
             << std::fixed << std::setprecision(3) << trigger.color[3] << ","
             << std::fixed << std::setprecision(2) << trigger.dimensions[0] << ","
             << std::fixed << std::setprecision(2) << trigger.dimensions[1] << ","
             << std::fixed << std::setprecision(2) << trigger.dimensions[2] << ","
             << std::fixed << std::setprecision(2) << trigger.dimensions[3] << ","
             << annotationDurationStr(trigger.durationType) << ","
             << trigger.durationMs << ","
             << trigger.leadTimeMs << ","
             << (trigger.enabled ? "true" : "false") << ","
             << (trigger.isAutoGenerated ? "true" : "false") << "\n";
    }

    return true;
}

// CachedAnnotationEvent dump
bool DebugDatabaseDumper::dumpCachedAnnotationEvents(const std::string& filepath, const AutoAnnotationTriggerManager* manager) {
    if (!manager) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "trigger_id,timestamp_ms,event_timestamp_ms,world_x,world_y,"
         << "source_x,source_y,has_source_position,target_guid,"
         << "sample_duration_ms,debuff_end_time_ms,is_during_debuff_sample\n";

    for (const auto& event : manager->getCachedEvents()) {
        file << event.triggerId << ","
             << event.timestamp_ms << ","
             << event.eventTimestamp_ms << ","
             << std::fixed << std::setprecision(2) << event.worldX << ","
             << std::fixed << std::setprecision(2) << event.worldY << ","
             << std::fixed << std::setprecision(2) << event.sourceX << ","
             << std::fixed << std::setprecision(2) << event.sourceY << ","
             << (event.hasSourcePosition ? "true" : "false") << ","
             << escapeCSV(event.targetGuid) << ","
             << event.sampleDurationMs << ","
             << event.debuffEndTime_ms << ","
             << (event.isDuringDebuffSample ? "true" : "false") << "\n";
    }

    return true;
}

// SpellNameMap dump
bool DebugDatabaseDumper::dumpSpellNameMap(const std::string& filepath, const std::unordered_map<uint32_t, std::string>* map) {
    if (!map) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "spell_id,spell_name\n";

    // Sort by spell_id for consistent output
    std::vector<std::pair<uint32_t, std::string>> sorted(map->begin(), map->end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (const auto& [spell_id, spell_name] : sorted) {
        file << spell_id << ","
             << escapeCSV(spell_name) << "\n";
    }

    return true;
}

// GuidToNameMap dump
bool DebugDatabaseDumper::dumpGuidToNameMap(const std::string& filepath, const std::unordered_map<std::string, std::string>* map) {
    if (!map) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "guid,actor_name\n";

    // Sort by GUID for consistent output
    std::vector<std::pair<std::string, std::string>> sorted(map->begin(), map->end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (const auto& [guid, name] : sorted) {
        file << escapeCSV(guid) << ","
             << escapeCSV(name) << "\n";
    }

    return true;
}

// PetToOwnerMap dump
bool DebugDatabaseDumper::dumpPetToOwnerMap(const std::string& filepath, const CombatDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "pet_guid,owner_guid\n";

    const auto& petMap = db->getPetToOwnerMap();

    // Resolve interned ids to strings, then sort by pet GUID for consistent output
    std::vector<std::pair<std::string, std::string>> sorted;
    sorted.reserve(petMap.size());
    for (const auto& [pet_id, owner_id] : petMap) {
        sorted.emplace_back(std::string(guidInterner().lookup(pet_id)),
                            std::string(guidInterner().lookup(owner_id)));
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (const auto& [pet_guid, owner_guid] : sorted) {
        file << escapeCSV(pet_guid) << ","
             << escapeCSV(owner_guid) << "\n";
    }

    return true;
}

// OwnerToPetsMap dump
bool DebugDatabaseDumper::dumpOwnerToPetsMap(const std::string& filepath, const CombatDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "owner_guid,pet_guids\n";

    const auto& ownerMap = db->getOwnerToPetsMap();

    // Resolve interned ids to strings, then sort by owner GUID for consistent output
    std::vector<std::pair<std::string, std::vector<std::string>>> sorted;
    sorted.reserve(ownerMap.size());
    for (const auto& [owner_id, pet_ids] : ownerMap) {
        std::vector<std::string> pets;
        pets.reserve(pet_ids.size());
        for (StringInterner::Id pet_id : pet_ids) {
            pets.emplace_back(guidInterner().lookup(pet_id));
        }
        sorted.emplace_back(std::string(guidInterner().lookup(owner_id)), std::move(pets));
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (const auto& [owner_guid, pet_guids] : sorted) {
        file << escapeCSV(owner_guid) << ",\"";
        for (size_t i = 0; i < pet_guids.size(); ++i) {
            if (i > 0) file << ",";
            file << pet_guids[i];
        }
        file << "\"\n";
    }

    return true;
}

// SpellsSeenByClass dump
bool DebugDatabaseDumper::dumpSpellsSeenByClass(const std::string& filepath, const ExtractedData* data) {
    if (!data) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "class_id,class_name,spell_id,spell_name\n";

    // Get seen classes and sort them
    auto classes = data->spellsSeenByClass.getSeenClasses();
    std::sort(classes.begin(), classes.end());

    for (auto wowClass : classes) {
        const auto& spells = data->spellsSeenByClass.getSpellsForClass(wowClass);

        // Sort spells by ID
        std::vector<std::pair<uint32_t, std::string>> sortedSpells(spells.begin(), spells.end());
        std::sort(sortedSpells.begin(), sortedSpells.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        for (const auto& [spell_id, spell_name] : sortedSpells) {
            file << static_cast<int>(wowClass) << ","
                 << wowClassStr(wowClass) << ","
                 << spell_id << ","
                 << escapeCSV(spell_name) << "\n";
        }
    }

    return true;
}

// AurasSeenByClass dump
bool DebugDatabaseDumper::dumpAurasSeenByClass(const std::string& filepath, const ExtractedData* data) {
    if (!data) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "class_id,class_name,spell_id,spell_name,is_buff\n";

    // Get seen classes and sort them
    auto classes = data->aurasSeenByClass.getSeenClasses();
    std::sort(classes.begin(), classes.end());

    for (auto wowClass : classes) {
        const auto& auras = data->aurasSeenByClass.getAurasForClass(wowClass);

        // Sort auras by ID
        std::vector<std::pair<uint32_t, AurasSeenByClass::AuraInfo>> sortedAuras(auras.begin(), auras.end());
        std::sort(sortedAuras.begin(), sortedAuras.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        for (const auto& [spell_id, info] : sortedAuras) {
            file << static_cast<int>(wowClass) << ","
                 << wowClassStr(wowClass) << ","
                 << spell_id << ","
                 << escapeCSV(info.spell_name) << ","
                 << (info.is_buff ? "true" : "false") << "\n";
        }
    }

    return true;
}

// Raw AuraEvents dump
bool DebugDatabaseDumper::dumpAuraEventsRaw(const std::string& filepath, const std::vector<AuraEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "target_guid,source_guid,spell_id,spell_name,aura_type,timestamp_ms,event_type,stacks\n";

    for (const auto& event : *events) {
        file << escapeCSV(event.target_guid) << ","
             << escapeCSV(event.source_guid) << ","
             << event.spell_id << ","
             << escapeCSV(event.spell_name) << ","
             << (event.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF") << ","
             << event.timestamp_ms << ","
             << auraEventTypeStr(event.event_type) << ","
             << static_cast<int>(event.stacks) << "\n";
    }

    return true;
}

// Secondary Index Dumps

// AuraDatabase sourceIndex dump - shows who is applying which auras
bool DebugDatabaseDumper::dumpAuraSourceIndex(const std::string& filepath, const AuraDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "source_guid,spell_id,spell_name,aura_type,target_count,instance_count\n";

    const auto& sourceIndex = db->getSourceIndex();

    // Aggregate by source+spell for summary statistics
    struct SpellSummary {
        std::string spell_name;
        AuraType aura_type = AuraType::Unknown;
        std::unordered_set<std::string> unique_targets;
        size_t instance_count = 0;
    };

    // source_guid -> spell_id -> SpellSummary
    std::map<std::string, std::map<uint32_t, SpellSummary>> aggregated;

    for (const auto& [source_guid, instances] : sourceIndex) {
        for (const AuraInstance* inst : instances) {
            auto& summary = aggregated[source_guid][inst->spell_id];
            if (summary.spell_name.empty()) {
                summary.spell_name = inst->spell_name;
                summary.aura_type = inst->aura_type;
            }
            summary.unique_targets.insert(inst->target_guid);
            summary.instance_count++;
        }
    }

    // Output sorted by source_guid, then spell_id
    for (const auto& [source_guid, spellMap] : aggregated) {
        for (const auto& [spell_id, summary] : spellMap) {
            file << escapeCSV(source_guid) << ","
                 << spell_id << ","
                 << escapeCSV(summary.spell_name) << ","
                 << (summary.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF") << ","
                 << summary.unique_targets.size() << ","
                 << summary.instance_count << "\n";
        }
    }

    return true;
}

// CombatDatabase damageTakenIndex dump - shows damage received by each target
bool DebugDatabaseDumper::dumpDamageTakenIndex(const std::string& filepath, const CombatDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "target_guid,total_damage,hit_count,max_hit,crit_count,source_count\n";

    const auto& damageTakenIndex = db->getDamageTakenIndex();

    // Aggregate stats per target
    struct TargetDamageStats {
        std::string target_guid;
        int64_t total_damage = 0;
        uint32_t hit_count = 0;
        int64_t max_hit = 0;
        uint32_t crit_count = 0;
        std::unordered_set<uint32_t> unique_sources;
    };

    std::map<std::string, TargetDamageStats> targetStats;

    // The records don't carry their source, so the sources come from walking
    // the damage-dealt tables the index was built from.
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> sourcesByTarget;
    if (const ActorMap* actorMap = db->getActorMap()) {
        for (const auto& [source_guid_id, table] : *actorMap) {
            for (const auto& record : table.damage_dealt_table) {
                if (record.target_guid_id != 0) {
                    sourcesByTarget[record.target_guid_id].insert(source_guid_id);
                }
            }
        }
    }

    for (const auto& [target_guid_id, records] : damageTakenIndex) {
        std::string target_guid = std::string(guidInterner().lookup(target_guid_id));
        auto& stats = targetStats[target_guid];
        stats.target_guid = target_guid;

        auto sourcesIt = sourcesByTarget.find(target_guid_id);
        if (sourcesIt != sourcesByTarget.end()) {
            stats.unique_sources.insert(sourcesIt->second.begin(), sourcesIt->second.end());
        }

        for (const CombatRecord* rec : records) {
            stats.total_damage += rec->effective_amount;
            stats.hit_count++;
            if (rec->effective_amount > stats.max_hit) {
                stats.max_hit = rec->effective_amount;
            }
            if (rec->flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
        }
    }

    // Output sorted by target_guid
    for (const auto& [target_guid, stats] : targetStats) {
        file << escapeCSV(target_guid) << ","
             << stats.total_damage << ","
             << stats.hit_count << ","
             << stats.max_hit << ","
             << stats.crit_count << ","
             << stats.unique_sources.size() << "\n";
    }

    return true;
}

// CombatDatabase healingReceivedIndex dump - shows healing received by each target
bool DebugDatabaseDumper::dumpHealingReceivedIndex(const std::string& filepath, const CombatDatabase* db) {
    if (!db) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "target_guid,total_healing,effective_healing,hit_count,max_heal,crit_count,overheal_count\n";

    const auto& healingReceivedIndex = db->getHealingReceivedIndex();

    // Aggregate stats per target
    struct TargetHealingStats {
        std::string target_guid;
        int64_t total_healing = 0;
        int64_t effective_healing = 0;
        uint32_t hit_count = 0;
        int64_t max_heal = 0;
        uint32_t crit_count = 0;
        uint32_t overheal_count = 0;
    };

    std::map<std::string, TargetHealingStats> targetStats;

    for (const auto& [target_guid_id, records] : healingReceivedIndex) {
        std::string target_guid = std::string(guidInterner().lookup(target_guid_id));
        auto& stats = targetStats[target_guid];
        stats.target_guid = target_guid;

        for (const CombatRecord* rec : records) {
            stats.total_healing += rec->amount;
            stats.effective_healing += rec->effective_amount;
            stats.hit_count++;
            if (rec->amount > stats.max_heal) {
                stats.max_heal = rec->amount;
            }
            if (rec->flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
            if (rec->flags & CombatEventFlags::Overheal) {
                stats.overheal_count++;
            }
        }
    }

    // Output sorted by target_guid
    for (const auto& [target_guid, stats] : targetStats) {
        file << escapeCSV(target_guid) << ","
             << stats.total_healing << ","
             << stats.effective_healing << ","
             << stats.hit_count << ","
             << stats.max_heal << ","
             << stats.crit_count << ","
             << stats.overheal_count << "\n";
    }

    return true;
}

// Missed and Absorb Event Dumps

// MissedEvents dump - all missed attacks (DODGE, PARRY, MISS, ABSORB, RESIST, etc.)
bool DebugDatabaseDumper::dumpMissedEvents(const std::string& filepath, const std::vector<MissedEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "timestamp_ms,formatted_time,source_guid,source_name,target_guid,target_name,"
         << "spell_id,spell_name,miss_type,amount_missed,blocked_amount,"
         << "is_offhand,is_critical,is_periodic\n";

    // Helper lambda to convert MissType to string
    auto missTypeStr = [](MissType mt) -> const char* {
        switch (mt) {
            case MissType::ABSORB: return "ABSORB";
            case MissType::BLOCK: return "BLOCK";
            case MissType::DEFLECT: return "DEFLECT";
            case MissType::DODGE: return "DODGE";
            case MissType::EVADE: return "EVADE";
            case MissType::IMMUNE: return "IMMUNE";
            case MissType::MISS: return "MISS";
            case MissType::PARRY: return "PARRY";
            case MissType::REFLECT: return "REFLECT";
            case MissType::RESIST: return "RESIST";
            default: return "UNKNOWN";
        }
    };

    for (const auto& event : *events) {
        file << event.timestamp_ms << ","
             << formatTimestamp(event.timestamp_ms) << ","
             << escapeCSV(event.source_guid) << ","
             << escapeCSV(event.source_name) << ","
             << escapeCSV(event.target_guid) << ","
             << escapeCSV(event.target_name) << ","
             << event.spell_id << ","
             << escapeCSV(event.spell_name) << ","
             << missTypeStr(event.miss_type) << ","
             << event.amount_missed << ","
             << event.blocked_amount << ","
             << (event.is_offhand ? "true" : "false") << ","
             << (event.is_critical ? "true" : "false") << ","
             << (event.is_periodic ? "true" : "false") << "\n";
    }

    return true;
}

// AbsorbEvents dump - shield absorption events
bool DebugDatabaseDumper::dumpAbsorbEvents(const std::string& filepath, const std::vector<AbsorbEvent>* events) {
    if (!events) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "timestamp_ms,formatted_time,source_guid,source_name,target_guid,target_name,"
         << "absorber_guid,absorber_name,source_spell_id,source_spell_name,"
         << "absorb_spell_id,absorb_spell_name,absorbed_amount,total_damage,supporter_guid\n";

    for (const auto& event : *events) {
        file << event.timestamp_ms << ","
             << formatTimestamp(event.timestamp_ms) << ","
             << escapeCSV(event.source_guid) << ","
             << escapeCSV(event.source_name) << ","
             << escapeCSV(event.target_guid) << ","
             << escapeCSV(event.target_name) << ","
             << escapeCSV(event.absorber_guid) << ","
             << escapeCSV(event.absorber_name) << ","
             << event.source_spell_id << ","
             << escapeCSV(event.source_spell_name) << ","
             << event.absorb_spell_id << ","
             << escapeCSV(event.absorb_spell_name) << ","
             << event.absorbed_amount << ","
             << event.total_damage << ","
             << escapeCSV(event.supporter_guid) << "\n";
    }

    return true;
}

#endif // NDEBUG
