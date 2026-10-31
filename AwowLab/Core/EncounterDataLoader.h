#pragma once
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

// All these types are owned by unique_ptr in DatabaseBundle, so full definitions needed
#include "Parser/Include/Actor/ActorMap.h"
#include "Database/EventDatabase.h"
#include "Database/SpellCastDatabase.h"
#include "Database/ResourceDatabase.h"
#include "Database/CombatDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/DispelDatabase.h"
#include "Database/AvoidanceDatabase.h"
#include "Database/EmoteDatabase.h"
#include "Database/ChargeManager.h"
#include "Database/SpellDataTable.h"
#include "Database/TalentDatabase.h"
#include "Database/CooldownCalculator.h"
#include "Database/CooldownReductionTable.h"
#include "Database/DatabaseFacade.h"

// Forward declarations
class UIManager;
struct ExtractedData;
struct CombatantInfoEvent;
struct MapChangeEvent;

namespace awow {
    class CooldownModifierRegistry;
}

// Bundle of all databases created from extracted encounter data.
// Owns all database objects - they are destroyed when the bundle goes out of scope.
struct DatabaseBundle {
    std::unique_ptr<EventDatabase> eventDb;
    std::unique_ptr<SpellCastDatabase> spellCastDb;
    std::unique_ptr<ResourceDatabase> resourceDb;
    std::unique_ptr<CombatDatabase> combatDb;
    std::unique_ptr<DeathDatabase> deathDb;
    std::unique_ptr<AuraDatabase> auraDb;
    std::unique_ptr<DispelDatabase> dispelDb;
    std::unique_ptr<AvoidanceDatabase> avoidanceDb;
    std::unique_ptr<EmoteDatabase> emoteDb;
    std::unique_ptr<ActorMap> actorMap;

    // Supporting objects (kept alive for database references)
    std::unique_ptr<awow::TalentDatabase> talentDb;
    std::unique_ptr<awow::CooldownReductionTable> reductionTable;
    std::unique_ptr<awow::CooldownCalculator> cooldownCalc;
    std::unique_ptr<awow::SpellDataTable> spellDataTable;
    std::unique_ptr<ChargeManager> chargeManager;

    // Health scaling data
    uint64_t avgPlayerHealth = 0;
    std::unordered_map<std::string, uint64_t> firstMaxHealthPerActor;

    // Check if bundle is valid (all required databases created)
    bool isValid() const {
        return eventDb && spellCastDb && resourceDb && combatDb &&
               deathDb && auraDb && dispelDb && avoidanceDb && actorMap;
    }

    // Create a non-owning facade from this bundle for passing to components
    DatabaseFacade createFacade() const {
        DatabaseFacade facade;
        facade.setEventDatabase(eventDb.get());
        facade.setSpellCastDatabase(spellCastDb.get());
        facade.setResourceDatabase(resourceDb.get());
        facade.setCombatDatabase(combatDb.get());
        facade.setDeathDatabase(deathDb.get());
        facade.setAuraDatabase(auraDb.get());
        facade.setDispelDatabase(dispelDb.get());
        facade.setAvoidanceDatabase(avoidanceDb.get());
        return facade;
    }
};

// Loads encounter data from ExtractedData into a DatabaseBundle.
// Consolidates the repetitive database creation pattern used across
// runEncounterLoop, runWarcraftLogsFlow, runLocalPlayAgain, and runPlayAlongMode.
class EncounterDataLoader {
public:
    EncounterDataLoader();
    ~EncounterDataLoader();

    // Load all databases from extracted data.
    // The ExtractedData is modified (auraEvents moved out).
    // Returns a bundle containing all created databases.
    DatabaseBundle loadFromExtractedData(ExtractedData& data);

    // Load the database bundle into UIManager for visualization.
    // Also loads spell names, combatant info, map changes, etc.
    void loadIntoUI(UIManager* ui, DatabaseBundle& bundle, ExtractedData& data,
                    uint32_t encounterId);

    // Enable/disable performance monitoring during load
    void setPerformanceMonitoring(bool enable) { perfMonitoring_ = enable; }

    // Enable/disable debug database dumping
    void setDebugDumping(bool enable, const std::string& prefix = "") {
        debugDumping_ = enable;
        debugPrefix_ = prefix;
    }

private:
    bool perfMonitoring_ = true;
    bool debugDumping_ = false;
    std::string debugPrefix_;

    // Internal helper to ensure modifier registry is loaded
    awow::CooldownModifierRegistry& ensureModifierRegistry();

    // Apply cooldowns to spell cast database using talent modifiers
    void applyCooldowns(SpellCastDatabase& spellCastDb,
                        awow::CooldownCalculator& cooldownCalc,
                        awow::SpellDataTable& spellDataTable);

    // Apply GCDs to spell cast database (pre-calculate with haste)
    void applyGCDs(SpellCastDatabase& spellCastDb,
                   awow::SpellDataTable& spellDataTable);

    // Compute health scaling from resource events
    void computeHealthScaling(const ExtractedData& data, DatabaseBundle& bundle);
};
