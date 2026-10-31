#include "CombatDatabase.h"
#include "Core/StringInterner.h"
#include "Core/MobWeightSettings.h"
#include <algorithm>
#include <iostream>

void CombatDatabase::loadFromActorMap(
    const ActorMap* actorMap,
    const std::unordered_map<StringInterner::Id, StringInterner::Id>* summonPetToOwner
) {
    actorMap_ = actorMap;
    min_timestamp_ = UINT32_MAX;
    max_timestamp_ = 0;
    petToOwnerMap_.clear();
    ownerToPetsMap_.clear();
    timeSeriesCache_.clear();

    if (!actorMap_ || actorMap_->empty()) {
        return;
    }

    // Find time bounds by scanning all combat tables
    for (const auto& [guid, table] : *actorMap_) {
        auto updateBounds = [this](const std::vector<CombatRecord>& records) {
            if (!records.empty()) {
                min_timestamp_ = std::min(min_timestamp_, records.front().timestamp_ms);
                max_timestamp_ = std::max(max_timestamp_, records.back().timestamp_ms);
            }
        };

        updateBounds(table.damage_dealt_table);
        updateBounds(table.healing_done_table);
    }

    if (min_timestamp_ == UINT32_MAX) {
        min_timestamp_ = 0;
    }

    buildPetToOwnerMap(summonPetToOwner);

    // Build reverse index: owner -> pets (for efficient pet iteration)
    for (const auto& [pet_id, owner_id] : petToOwnerMap_) {
        ownerToPetsMap_[owner_id].push_back(pet_id);
    }

    buildDamageTakenIndex();

    buildHealingReceivedIndex();

    // Resolve per-target damage weights from the user's mob weighting
    refreshTargetWeights();
}

void CombatDatabase::refreshTargetWeights() const {
    targetWeights_.clear();
    timeSeriesCache_.clear();  // Cached series were built with old weights

    const auto& settings = MobWeightSettings::instance();
    if (!settings.enabled || settings.weights.empty()) {
        return;
    }

    // Every damage target appears as a key in the damage-taken index,
    // so one pass over it covers all guids the aggregators will see
    for (const auto& [target_guid_id, records] : damageTakenIndex_) {
        std::string_view guid = guidInterner().lookup(target_guid_id);
        uint32_t npcId = MobWeightSettings::npcIdFromGuid(guid);
        if (npcId == 0) continue;

        float weight = settings.weightFor(npcId);
        if (weight < 0.9995f) {
            targetWeights_[target_guid_id] = weight;
        }
    }
}

void CombatDatabase::buildPetToOwnerMap(
    const std::unordered_map<StringInterner::Id, StringInterner::Id>* summonPetToOwner
) {
    if (!actorMap_) return;

    // NPC-id -> summoning player, built from the summon lineage. WoW reuses
    // one NPC id across many spawns of the same summon (every "Lesser Ghoul"
    // or Army horseman shares an id, only the spawn suffix differs), and a
    // fresh spawn that reused an id can act before - or without - its own
    // SPELL_SUMMON line landing in this segment. Below we let such a spawn
    // inherit its NPC id's owner, but only when that id belongs to exactly
    // one summoning player, so a possessed/mind-controlled add (whose id no
    // player ever summoned) can never sneak into a player.
    std::unordered_map<uint32_t, StringInterner::Id> npcIdToSummoner;
    std::unordered_set<uint32_t> ambiguousNpcIds;
    if (summonPetToOwner) {
        for (const auto& [pet_id, owner_id] : *summonPetToOwner) {
            uint32_t npcId = MobWeightSettings::npcIdFromGuid(guidInterner().lookup(pet_id));
            if (npcId == 0) continue;
            auto [it, inserted] = npcIdToSummoner.try_emplace(npcId, owner_id);
            if (!inserted && it->second != owner_id) {
                ambiguousNpcIds.insert(npcId);  // summoned by more than one player
            }
        }
    }

    // First, the SPELL_SUMMON lineage. The summoner names the unit directly,
    // and it is the only way a player can claim a Creature-/Vehicle- pet (a
    // Death Knight's ghouls, a Magus of the Dead, an elemental) whose own
    // damage events carry no owner. Only pets that act in the log are added.
    if (summonPetToOwner) {
        for (const auto& [pet_id, owner_id] : *summonPetToOwner) {
            if (pet_id == owner_id) continue;                 // self-ownership
            if (actorMap_->find(pet_id) == actorMap_->end()) continue;  // pet never acted
            petToOwnerMap_[pet_id] = owner_id;
        }
    }

    // Then the combat records, for what the summon map didn't cover. This is
    // where enemy-side ownership comes from, so the Enemy Damage meter folds
    // a boss's summons into the boss row the way the player meter folds a
    // hunter's pet into the hunter.
    for (const auto& [guid_id, table] : *actorMap_) {
        // Already set from the summon lineage
        if (petToOwnerMap_.find(guid_id) != petToOwnerMap_.end()) {
            continue;
        }

        // Only Pet-, Creature- and Vehicle- guids can have owners
        std::string_view guid_sv = guidInterner().lookup(guid_id);
        if (guid_sv.starts_with("Player-")) {
            continue;
        }

        // A player owner is only adopted here onto a real Pet- guid. A boss
        // hitting a player's pet, or a mind-controlled add, can show a player
        // owner_guid on a Creature-/Vehicle- actor without being that
        // player's summon, and folding it in would count the enemy's damage
        // as the player's. Real player summons were already handled by the
        // summon lineage above. Enemy guardians and totems still merge into
        // their non-player owner.
        const bool actorIsRealPet = guid_sv.starts_with("Pet-");

        // A Creature- pet whose own SPELL_SUMMON is not in this segment, but
        // whose NPC id belongs to exactly one summoning player, is that
        // player's. Its damage events may never name an owner; only its
        // SPELL_CAST_SUCCESS advanced block does, and that is not a combat
        // record. A possessed add has an NPC id no player summoned.
        StringInterner::Id summonedNpcOwner = 0;
        if (!actorIsRealPet) {
            uint32_t npcId = MobWeightSettings::npcIdFromGuid(guid_sv);
            if (npcId != 0 && ambiguousNpcIds.find(npcId) == ambiguousNpcIds.end()) {
                auto it = npcIdToSummoner.find(npcId);
                if (it != npcIdToSummoner.end()) summonedNpcOwner = it->second;
            }
        }

        if (summonedNpcOwner != 0) {
            petToOwnerMap_[guid_id] = summonedNpcOwner;
            continue;
        }

        // A pet's owner_guid is only reliable on some events: a
        // Plaguestalker's SWING records name the player, while its
        // SPELL_DAMAGE records can carry the target's info block (a stray
        // Creature owner). So a player owner wins over any non-player owner.
        // Other actors take the first valid non-player owner.
        StringInterner::Id playerOwner = 0;   // an accepted Player- owner
        StringInterner::Id fallbackOwner = 0; // first valid non-player owner

        auto scanTable = [&](const std::vector<CombatRecord>& records) {
            for (const auto& record : records) {
                if (record.owner_guid_id == guid_id) continue;  // self-ownership
                std::string_view owner_sv = guidInterner().lookup(record.owner_guid_id);
                if (owner_sv.empty() || owner_sv == "0000000000000000") {
                    continue;
                }

                if (owner_sv.starts_with("Player-")) {
                    if (actorIsRealPet && playerOwner == 0) {
                        playerOwner = record.owner_guid_id;
                    }
                } else if (fallbackOwner == 0) {
                    fallbackOwner = record.owner_guid_id;
                }
            }
        };

        scanTable(table.damage_dealt_table);
        scanTable(table.healing_done_table);

        StringInterner::Id owner = playerOwner != 0 ? playerOwner : fallbackOwner;
        if (owner != 0) {
            petToOwnerMap_[guid_id] = owner;
        }
    }

}

std::string CombatDatabase::getOwnerGuid(const std::string& pet_guid) const {
    // find() never grows the interner pool.
    StringInterner::Id pet_id = guidInterner().find(pet_guid);
    if (pet_id == StringInterner::INVALID) return "";

    auto it = petToOwnerMap_.find(pet_id);
    if (it == petToOwnerMap_.end()) return "";
    return std::string(guidInterner().lookup(it->second));
}

bool CombatDatabase::isPetOrGuardian(const std::string& actor_guid) const {
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) return false;
    return petToOwnerMap_.find(actor_id) != petToOwnerMap_.end();
}

const std::vector<CombatRecord>* CombatDatabase::getCombatTable(
    const ActorTable& actor_table,
    CombatMetricType type
) const {
    switch (type) {
        case CombatMetricType::DamageDealt:
            return &actor_table.damage_dealt_table;
        case CombatMetricType::HealingDone:
            return &actor_table.healing_done_table;
        default:
            return &actor_table.damage_dealt_table;
    }
}
