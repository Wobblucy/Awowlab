#pragma once

// Only include debug database dumper in debug builds
#ifndef NDEBUG

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include "Actor/ActorMap.h"

// Forward declarations
class EventDatabase;
class SpellCastDatabase;
class ResourceDatabase;
class CombatDatabase;
class DeathDatabase;
class AuraDatabase;
class AutoAnnotationTriggerManager;
struct CombatantInfoEvent;
struct MapChangeEvent;
struct SummonEvent;
struct ResurrectEvent;
struct AuraEvent;
struct MissedEvent;
struct AbsorbEvent;

// Forward declarations for ExtractedData nested structs
namespace parser { enum class WowClass : uint8_t; }
struct ExtractedData;

// DebugDatabaseDumper - Exports all database contents to CSV files for debugging
// Writes one CSV per database or table into a "debuglogs" subfolder
// Debug builds only
class DebugDatabaseDumper {
public:
    // Dump all databases to the specified output directory
    // Creates subfolder "debuglogs" if it doesn't exist
    // sourcePrefix: "loc_" for local files, "wcl_" for WarcraftLogs
    // Returns true if all exports succeeded
    static bool dumpAll(
        const std::string& outputDir,
        const std::string& sourcePrefix,
        const EventDatabase* eventDb,
        const SpellCastDatabase* spellCastDb,
        const ResourceDatabase* resourceDb,
        const CombatDatabase* combatDb,
        const DeathDatabase* deathDb,
        const AuraDatabase* auraDb,
        const ActorMap* actorMap,
        // Additional extracted data not in databases
        const std::vector<CombatantInfoEvent>* combatantInfo = nullptr,
        const std::vector<MapChangeEvent>* mapChanges = nullptr,
        const std::vector<SummonEvent>* summons = nullptr,
        const std::vector<ResurrectEvent>* resurrects = nullptr,
        // Annotation data
        const AutoAnnotationTriggerManager* annotationTriggers = nullptr,
        // Lookup maps
        const std::unordered_map<uint32_t, std::string>* spellNameMap = nullptr,
        const std::unordered_map<std::string, std::string>* guidToNameMap = nullptr,
        // Raw aura events
        const std::vector<AuraEvent>* auraEventsRaw = nullptr,
        // Extracted data (for class-based tracking)
        const ExtractedData* extractedData = nullptr
    );

    // Individual dump methods for each database type
    static bool dumpEventDatabase(const std::string& filepath, const EventDatabase* db);
    static bool dumpSpellCastDatabase(const std::string& filepath, const SpellCastDatabase* db);
    static bool dumpCombatDatabase(const std::string& filepath, const CombatDatabase* db);
    static bool dumpDeathDatabase(const std::string& filepath, const DeathDatabase* db);
    static bool dumpAuraDatabase(const std::string& filepath, const AuraDatabase* db);

    // ActorMap table dumps (one file per table type)
    static bool dumpActorMapSpellCasts(const std::string& filepath, const ActorMap* actorMap);
    static bool dumpActorMapResources(const std::string& filepath, const ActorMap* actorMap);
    static bool dumpActorMapDamageDealt(const std::string& filepath, const ActorMap* actorMap);
    static bool dumpActorMapHealingDone(const std::string& filepath, const ActorMap* actorMap);

    // Additional extracted data dumps
    static bool dumpCombatantInfo(const std::string& filepath, const std::vector<CombatantInfoEvent>* events);
    static bool dumpMapChanges(const std::string& filepath, const std::vector<MapChangeEvent>* events);
    static bool dumpSummons(const std::string& filepath, const std::vector<SummonEvent>* events);
    static bool dumpResurrects(const std::string& filepath, const std::vector<ResurrectEvent>* events);

    // Auto-Annotation dumps
    static bool dumpAutoAnnotationTriggers(const std::string& filepath, const AutoAnnotationTriggerManager* manager);
    static bool dumpCachedAnnotationEvents(const std::string& filepath, const AutoAnnotationTriggerManager* manager);

    // Lookup map dumps
    static bool dumpSpellNameMap(const std::string& filepath, const std::unordered_map<uint32_t, std::string>* map);
    static bool dumpGuidToNameMap(const std::string& filepath, const std::unordered_map<std::string, std::string>* map);
    static bool dumpPetToOwnerMap(const std::string& filepath, const CombatDatabase* db);
    static bool dumpOwnerToPetsMap(const std::string& filepath, const CombatDatabase* db);

    // Class tracking dumps
    static bool dumpSpellsSeenByClass(const std::string& filepath, const ExtractedData* data);
    static bool dumpAurasSeenByClass(const std::string& filepath, const ExtractedData* data);

    // Raw event dumps
    static bool dumpAuraEventsRaw(const std::string& filepath, const std::vector<AuraEvent>* events);

    // Secondary index dumps
    static bool dumpAuraSourceIndex(const std::string& filepath, const AuraDatabase* db);
    static bool dumpDamageTakenIndex(const std::string& filepath, const CombatDatabase* db);
    static bool dumpHealingReceivedIndex(const std::string& filepath, const CombatDatabase* db);

    // Missed and Absorb event dumps
    static bool dumpMissedEvents(const std::string& filepath, const std::vector<MissedEvent>* events);
    static bool dumpAbsorbEvents(const std::string& filepath, const std::vector<AbsorbEvent>* events);

private:
    // Helper to escape CSV fields
    static std::string escapeCSV(std::string_view field);

    // Helper to create directory if needed
    static bool ensureDirectory(const std::string& path);

    // Helper to format timestamp in ms to MM:SS.mmm (supports negative for pre-encounter)
    static std::string formatTimestamp(int32_t timestamp_ms);
};

#endif // NDEBUG
