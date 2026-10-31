#include "EncounterDataLoader.h"
#include "PerfMonitor.h"
#include "SpellNameDatabase.h"
#include "CurrentEncounter.h"
#include "Database/OutputWriter.h"
#include "Database/EventDatabase.h"
#include "Database/SpellCastDatabase.h"
#include "Database/ResourceDatabase.h"
#include "Database/CombatDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/DispelDatabase.h"
#include "Database/AvoidanceDatabase.h"
#include "Database/TalentDatabase.h"
#include "Database/CooldownCalculator.h"
#include "Database/CooldownModifiers/CooldownModifierRegistry.h"
#include "Database/CooldownReductionTable.h"
#include "Database/ChargeManager.h"
#include "Database/SpellDataTable.h"
#include "Parser/Include/Actor/ActorMap.h"
#include "Parser/Include/Haste/HasteTracker.h"
#include "UIManager/include/Core/UIManager.h"
#include "Utils/ExecutablePath.h"
#include "UnifiedSettings.h"
#include "Benchmark/BenchmarkSession.h"
#ifndef NDEBUG
#include "Database/DebugDatabaseDumper.h"
#endif
#include <algorithm>

EncounterDataLoader::EncounterDataLoader() = default;
EncounterDataLoader::~EncounterDataLoader() = default;

awow::CooldownModifierRegistry& EncounterDataLoader::ensureModifierRegistry() {
    auto& modifierRegistry = awow::CooldownModifierRegistry::instance();
    if (modifierRegistry.modifierCount() == 0) {
        std::string spellDataVersion = SettingsCache::instance().get().spellDataVersion;
        std::string modifierPath = awow::getExecutableDirectory() +
            "data\\cooldown_modifiers_" + spellDataVersion + ".json";
        modifierRegistry.loadFromJson(modifierPath);
    }
    return modifierRegistry;
}

void EncounterDataLoader::applyCooldowns(SpellCastDatabase& spellCastDb,
                                          awow::CooldownCalculator& cooldownCalc,
                                          awow::SpellDataTable& spellDataTable) {
    for (auto& [guid, castList] : spellCastDb.getMutableActorCasts()) {
        for (auto& cast : castList) {
            int32_t base_cooldown = spellDataTable.getBaseCooldown(cast.spell_id);

            int32_t modified_cooldown = cooldownCalc.calculateCooldown(
                cast.actor_guid,
                cast.spell_id,
                base_cooldown
            );

            cast.cooldown_ms = modified_cooldown;
            cast.cooldown_ready_ms = cast.end_timestamp_ms + modified_cooldown;
        }
    }
}

void EncounterDataLoader::applyGCDs(SpellCastDatabase& spellCastDb,
                                     awow::SpellDataTable& spellDataTable) {
    for (auto& [guid, castList] : spellCastDb.getMutableActorCasts()) {
        for (auto& cast : castList) {
            int32_t base_gcd = spellDataTable.getBaseGCD(cast.spell_id);

            if (base_gcd > 0 && cast.haste_permille > 0) {
                float hasteMultiplier = 1.0f + (cast.haste_permille / 1000.0f);
                int32_t actualGcd = static_cast<int32_t>(base_gcd / hasteMultiplier);

                // Apply GCD floor (750ms for 1.5s base, 500ms for 1.0s base)
                int32_t gcdFloor = (base_gcd >= 1500) ? 750 : 500;
                cast.gcd_ms = std::max(actualGcd, gcdFloor);
            } else {
                // No haste known, or an off-GCD spell
                cast.gcd_ms = base_gcd;
            }
        }
    }
}

void EncounterDataLoader::computeHealthScaling(const ExtractedData& data, DatabaseBundle& bundle) {
    uint64_t totalPlayerHealth = 0;
    size_t playerCount = 0;

    for (const auto& resEvent : data.resourceEvents) {
        if (bundle.firstMaxHealthPerActor.find(resEvent.actor_guid) != bundle.firstMaxHealthPerActor.end()) {
            continue;
        }

        bundle.firstMaxHealthPerActor[resEvent.actor_guid] = resEvent.max_health;

        if (resEvent.actor_type == ::ActorType::Player && resEvent.flags.isFriendly()) {
            totalPlayerHealth += resEvent.max_health;
            playerCount++;
        }
    }

    bundle.avgPlayerHealth = (playerCount > 0) ? (totalPlayerHealth / playerCount) : 0;

}

DatabaseBundle EncounterDataLoader::loadFromExtractedData(ExtractedData& data) {
    DatabaseBundle bundle;

    if (perfMonitoring_) {
        AWOW_PERF_BEGIN_SESSION("Encounter Load");
        AWOW_PERF_MEMORY_SNAPSHOT("Before Database Creation");
    }

    // Lazy-load spell data (names + icon mappings) for spell IDs found in this encounter
    auto spellIds = data.collectAllSpellIds();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("SpellNameDatabase::loadSpellIds"); }
        SpellNameDatabase::instance().loadSpellIds(spellIds);
    }

    bundle.eventDb = std::make_unique<EventDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("EventDatabase::loadEvents"); }
        bundle.eventDb->loadEvents(data.actorEvents);
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("EventDatabase", bundle.eventDb->size(),
                             bundle.eventDb->size() * 28, "28 bytes/ActorEventCompact");
    }

    bundle.spellCastDb = std::make_unique<SpellCastDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("SpellCastDatabase::loadSpellCasts"); }

        // WCL imports carry no per-cast haste, so it is rebuilt from auras
        if (data.fromImportedReport) {
            haste::HasteTracker hasteTracker;

            // Initialize base haste from COMBATANT_INFO events
            for (const auto& combatant : data.combatantInfoEvents) {
                // Use the highest haste rating (melee/ranged/spell)
                int32_t maxHaste = std::max(combatant.haste_melee,
                                            std::max(combatant.haste_ranged,
                                                     combatant.haste_spell));
                hasteTracker.setBaseHasteFromRating(combatant.player_guid, maxHaste);
            }

            // Build haste timeline from aura events (buffs/debuffs that affect haste)
            int hasteBuffsTracked = 0;
            for (const auto& aura : data.auraEvents) {
                bool tracked = false;
                if (aura.event_type == AuraEventType::Applied) {
                    tracked = hasteTracker.applyBuff(aura.target_guid, aura.spell_id, aura.stacks, aura.timestamp_ms);
                } else if (aura.event_type == AuraEventType::Removed) {
                    tracked = hasteTracker.removeBuff(aura.target_guid, aura.spell_id, aura.timestamp_ms);
                } else if (aura.event_type == AuraEventType::AppliedDose ||
                           aura.event_type == AuraEventType::RemovedDose ||
                           aura.event_type == AuraEventType::Refresh) {
                    tracked = hasteTracker.updateBuffStacks(aura.target_guid, aura.spell_id, aura.stacks, aura.timestamp_ms);
                }
                if (tracked) hasteBuffsTracked++;

            }

            int castsWithHaste = 0;
            int32_t minHaste = INT32_MAX, maxHaste = 0;
            for (auto& cast : data.spellCastEvents) {
                if (hasteTracker.hasPlayer(cast.actor_guid)) {
                    // Use cast start time for haste lookup (haste at cast start determines GCD)
                    int32_t castTime = cast.start_timestamp_ms > 0
                        ? cast.start_timestamp_ms
                        : cast.end_timestamp_ms;
                    int32_t haste = hasteTracker.getHasteAtTime(cast.actor_guid, castTime);
                    cast.haste_permille = static_cast<uint16_t>(haste);
                    if (haste > 0) {
                        castsWithHaste++;
                        minHaste = std::min(minHaste, haste);
                        maxHaste = std::max(maxHaste, haste);
                    }
                }
            }

        }

        bundle.spellCastDb->loadSpellCasts(data.spellCastEvents);
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("SpellCastDatabase", bundle.spellCastDb->size(),
                             bundle.spellCastDb->size() * 48, "~48 bytes/SpellCastEvent");
    }

    bundle.reductionTable = std::make_unique<awow::CooldownReductionTable>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("CooldownReductionTable::loadReductions"); }
        bundle.reductionTable->loadReductions(data.cooldownReductions);
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("CooldownReductionTable", bundle.reductionTable->recordCount(),
                             bundle.reductionTable->recordCount() * 16, "16 bytes/CooldownReductionRecord");
    }

    bundle.spellCastDb->setReductionTable(bundle.reductionTable.get());

    bundle.talentDb = std::make_unique<awow::TalentDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("TalentDatabase::loadFromCombatantInfo"); }
        bundle.talentDb->loadFromCombatantInfo(data.combatantInfoEvents);
    }

    auto& modifierRegistry = ensureModifierRegistry();
    bundle.cooldownCalc = std::make_unique<awow::CooldownCalculator>(
        bundle.talentDb.get(), &modifierRegistry);

    bundle.spellDataTable = std::make_unique<awow::SpellDataTable>();
    std::string spellDataVersion = SettingsCache::instance().get().spellDataVersion;
    std::string binPath = awow::getExecutableDirectory() + "data\\spell_data_" + spellDataVersion + ".bin";

    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("SpellDataTable::loadAndApplyCooldowns"); }
        if (bundle.spellDataTable->loadFromBinary(binPath)) {
            applyCooldowns(*bundle.spellCastDb,
                          *bundle.cooldownCalc, *bundle.spellDataTable);
            applyGCDs(*bundle.spellCastDb, *bundle.spellDataTable);
        }
    }

    bundle.chargeManager = std::make_unique<ChargeManager>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("ChargeManager::loadFromJson"); }
        std::string chargeSpellsPath = awow::getExecutableDirectory() + "data\\charge_spells.json";
        bundle.chargeManager->loadFromJson(chargeSpellsPath);
        bundle.chargeManager->setTalentDatabase(bundle.talentDb.get());
        bundle.chargeManager->setModifierRegistry(&modifierRegistry);
        bundle.chargeManager->setSpellDataTable(bundle.spellDataTable.get());
    }
    bundle.spellCastDb->setChargeManager(bundle.chargeManager.get());

    bundle.actorMap = std::make_unique<ActorMap>();
    // Authoritative pet -> owner lineage from SPELL_SUMMON, handed to the
    // CombatDatabase so a player's summoned Creature-/Vehicle- pets (a Death
    // Knight's army, etc.) merge into the player even when the advanced
    // combat log never tags them with an owner.
    auto summonPetToOwner = OutputWriter::buildSummonPetToOwnerMap(data);
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("OutputWriter::buildActorMap"); }
        *bundle.actorMap = OutputWriter::buildActorMap(data);
    }
    if (perfMonitoring_) {
        AWOW_PERF_MEMORY_SNAPSHOT("After ActorMap");
    }

    bundle.resourceDb = std::make_unique<ResourceDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("ResourceDatabase::loadFromActorMap"); }
        bundle.resourceDb->loadFromActorMap(bundle.actorMap.get());
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("ResourceDatabase", bundle.resourceDb->size(),
                             bundle.resourceDb->size() * 32, "32 bytes/ResourceStatusRecord");
    }

    bundle.combatDb = std::make_unique<CombatDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("CombatDatabase::loadFromActorMap"); }
        bundle.combatDb->loadFromActorMap(bundle.actorMap.get(), &summonPetToOwner);
    }
    if (perfMonitoring_) {
        AWOW_PERF_MEMORY_SNAPSHOT("After CombatDatabase");
    }

    // DeathDatabase reads eventDb to infer resurrections the log does not state
    bundle.deathDb = std::make_unique<DeathDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("DeathDatabase::loadEvents"); }
        bundle.deathDb->loadEvents(data.deathEvents, data.resurrectEvents, bundle.eventDb.get());
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("DeathDatabase", bundle.deathDb->getAllDeaths().size(),
                             bundle.deathDb->getAllDeaths().size() * 80, "~80 bytes/DeathEvent");
    }

    bundle.auraDb = std::make_unique<AuraDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("AuraDatabase::loadFromEvents"); }
        bundle.auraDb->loadFromEvents(std::move(data.auraEvents), data.firstSeenTimestamps);
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("AuraDatabase", bundle.auraDb->targetCount(),
                             bundle.auraDb->targetCount() * 100, "~100 bytes/target avg");
    }

    bundle.dispelDb = std::make_unique<DispelDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("DispelDatabase::loadFromEvents"); }
        bundle.dispelDb->loadFromEvents(data.dispelInterruptEvents);
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("DispelDatabase", bundle.dispelDb->size(),
                             bundle.dispelDb->size() * 60, "~60 bytes/event avg");
    }

    // Dodges, parries, blocks and misses
    bundle.avoidanceDb = std::make_unique<AvoidanceDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("AvoidanceDatabase::loadFromEvents"); }
        bundle.avoidanceDb->loadFromEvents(data.missedEvents);
    }
    if (perfMonitoring_) {
        AWOW_PERF_DB_METRICS("AvoidanceDatabase", bundle.avoidanceDb->size(),
                             bundle.avoidanceDb->size() * 40, "~40 bytes/event avg");
    }

    // Boss emote lines, for phase rules
    bundle.emoteDb = std::make_unique<EmoteDatabase>();
    {
        if (perfMonitoring_) { AWOW_PERF_SCOPE("EmoteDatabase::loadFromEvents"); }
        bundle.emoteDb->loadFromEvents(data.emoteEvents);
    }

    computeHealthScaling(data, bundle);

    if (perfMonitoring_) {
        AWOW_PERF_MEMORY_SNAPSHOT("After All Databases");
        AWOW_PERF_PRINT_SUMMARY();
    }

#ifndef NDEBUG
    if (debugDumping_) {
        DebugDatabaseDumper::dumpAll(
            ".",  // Current directory
            debugPrefix_.c_str(),
            bundle.eventDb.get(),
            bundle.spellCastDb.get(),
            bundle.resourceDb.get(),
            bundle.combatDb.get(),
            bundle.deathDb.get(),
            bundle.auraDb.get(),
            bundle.actorMap.get(),
            &data.combatantInfoEvents,
            &data.mapChangeEvents,
            &data.summonEvents,
            &data.resurrectEvents,
            nullptr,  // annotationTriggers - not available at this point
            &data.spellNameMap,
            &data.guidToNameMap,
            &data.auraEvents,
            &data
        );
    }
#endif

    return bundle;
}

void EncounterDataLoader::loadIntoUI(UIManager* ui, DatabaseBundle& bundle, ExtractedData& data,
                                      uint32_t encounterId) {
    if (!ui || !bundle.isValid()) return;

    auto spellIds = data.collectAllSpellIds();

    // Load MAP_CHANGE events first (needed for initial map lookup)
    ui->loadMapChangeEvents(data.mapChangeEvents, encounterId);
    // Update CurrentEncounter with first valid map_id for snapshot export
    if (!data.mapChangeEvents.empty() && data.mapChangeEvents[0].map_id != 0) {
        CurrentEncounter::instance().setMapId(static_cast<uint16_t>(data.mapChangeEvents[0].map_id));
    }

    ui->loadEventDatabase(bundle.eventDb.get());
    ui->loadSpellCastDatabase(bundle.spellCastDb.get());
    ui->loadResourceDatabase(bundle.resourceDb.get());
    ui->loadCombatDatabase(bundle.combatDb.get());
    ui->loadDeathDatabase(bundle.deathDb.get());
    ui->loadAuraDatabase(bundle.auraDb.get());
    ui->loadDispelDatabase(bundle.dispelDb.get());
    ui->loadAvoidanceDatabase(bundle.avoidanceDb.get());
    ui->loadEmoteDatabase(bundle.emoteDb.get());

    ui->loadSpellNameMap(data.spellNameMap);
    ui->loadSpellDataForIds(spellIds);
    ui->loadGuidToNameMap(data.guidToNameMap);
    ui->loadActorNpcIdMap(data.actorIdToNpcId);
    ui->loadCombatantInfo(data.combatantInfoEvents);
    ui->generateFallbackColors();
    ui->loadHealthScalingData(bundle.avgPlayerHealth, bundle.firstMaxHealthPerActor);

    // Kept for snapshot export
    ui->setExtractedData(&data);

    // The benchmark session gets the same pointer, so a reference can be
    // re-cut when a ruling changes without the panels knowing where the log
    // came from. Every load passes through here, and whoever replaces the
    // data comes back through here too, so the lifetime matches
    // setExtractedData.
    benchmark::BenchmarkSession::instance().setLiveExtractedData(&data);
}
