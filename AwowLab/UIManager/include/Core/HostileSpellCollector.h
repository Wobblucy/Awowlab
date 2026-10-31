#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <utility>  // for std::pair
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "structures.h"  // for UnitFlags

class SpellCastDatabase;
class AuraDatabase;
class CombatDatabase;
class EventDatabase;
class ActorColorGenerator;

// Collects spell/debuff/damage information from hostile creatures across multiple databases.
// Groups by creature NAME (not GUID) so triggers apply to all instances of the same enemy.
// Used by auto-annotation system and boss timer to identify available abilities.
class HostileSpellCollector {
public:
    HostileSpellCollector() = default;

    // Build the hostile creature spells list from all databases
    // Groups by creature name (stripping instance suffixes like " (1)")
    // Returns vector sorted by creature name (Environment last)
    std::vector<HostileCreatureSpells> collect(
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        const CombatDatabase* combatDb,
        const EventDatabase* eventDb,
        const ActorColorGenerator* colorGen
    );

    // Strip WCL instance suffix " (N)" from actor names for grouping
    // "Concentrated Void (1)" -> "Concentrated Void"
    // "Big M.O.M.M.A." -> "Big M.O.M.M.A." (unchanged)
    static std::string stripInstanceSuffix(std::string_view name);

private:
    // Internal data structure for accumulating spell data per creature
    struct CreatureSpellData {
        std::unordered_set<uint32_t> spell_ids;         // Casts
        std::unordered_set<uint32_t> debuff_ids;        // Debuffs applied
        std::unordered_set<uint32_t> damage_ids;        // Damage dealt
        std::unordered_set<uint32_t> buff_ids;          // Buffs applied
        std::unordered_set<uint32_t> interruptible_ids; // Spells with cast bar
    };

    // Scan spell casts for casts by hostile/neutral creatures
    void scanSpellCasts(
        const SpellCastDatabase* spellCastDb,
        const ActorColorGenerator* colorGen,
        std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
    );

    // Scan aura instances for debuffs applied by hostile/neutral creatures onto players
    void scanDebuffs(
        const AuraDatabase* auraDb,
        const ActorColorGenerator* colorGen,
        std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
    );

    // Scan aura instances for buffs applied by hostile/neutral creatures (e.g., enemy self-buffs)
    void scanBuffs(
        const AuraDatabase* auraDb,
        const ActorColorGenerator* colorGen,
        std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
    );

    // Scan damage dealt records for damage spell IDs from hostile/neutral creatures
    void scanDamageDealt(
        const CombatDatabase* combatDb,
        const ActorColorGenerator* colorGen,
        std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
    );

    // Scan EventDatabase for ALL hostile/neutral actors (ensures actors that just "exist" appear)
    void scanActorPresence(
        const EventDatabase* eventDb,
        const ActorColorGenerator* colorGen,
        std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
    );

    // Convert accumulated data to final output vector
    std::vector<HostileCreatureSpells> buildOutput(
        std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
    );

    // Helper to find actor by GUID prefix (handles WCL truncated GUIDs)
    // WCL aura events use truncated GUIDs like "Creature-0-0-0-0-60"
    // but position events use full GUIDs like "Creature-0-0-0-0-60-1"
    // Returns {name, flags} pair, or {empty, nullptr} if not found
    std::pair<std::string_view, const UnitFlags*> findActorByGuidPrefix(
        const ActorColorGenerator* colorGen,
        std::string_view guidPrefix
    );
};
