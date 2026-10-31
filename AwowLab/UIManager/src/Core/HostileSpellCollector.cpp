#include "Core/HostileSpellCollector.h"
#include "Color/ActorColorGenerator.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/AuraDatabase.h"
#include "../../Database/CombatDatabase.h"
#include "../../Database/EventDatabase.h"
#include <algorithm>

std::pair<std::string_view, const UnitFlags*> HostileSpellCollector::findActorByGuidPrefix(
    const ActorColorGenerator* colorGen,
    std::string_view guidPrefix
) {
    if (!colorGen) return {{}, nullptr};

    const UnitFlags* flags = colorGen->getActorFlags(guidPrefix);
    if (flags) {
        return {colorGen->getActorName(guidPrefix), flags};
    }

    // WCL aura events use truncated GUIDs ("Creature-0-0-0-0-60") while position
    // events carry the full one ("Creature-0-0-0-0-60-1"), so fall back to a prefix match
    for (const auto& [guid, actorFlags] : colorGen->getActorFlags()) {
        if (guid.starts_with(guidPrefix) && guid.size() > guidPrefix.size()) {
            // The next character must start an instance suffix
            if (guid[guidPrefix.size()] == '-') {
                if (actorFlags.isHostile() || actorFlags.isNeutral()) {
                    return {colorGen->getActorName(guid), &actorFlags};
                }
            }
        }
    }
    return {{}, nullptr};
}

std::string HostileSpellCollector::stripInstanceSuffix(std::string_view name) {
    if (name.size() < 4) return std::string(name);

    // Check for pattern " (N)" or " (NN)" at end
    size_t parenClose = name.rfind(')');
    if (parenClose != name.size() - 1) return std::string(name);

    size_t parenOpen = name.rfind('(');
    if (parenOpen == std::string_view::npos || parenOpen < 2) return std::string(name);

    if (name[parenOpen - 1] != ' ') return std::string(name);

    std::string_view between = name.substr(parenOpen + 1, parenClose - parenOpen - 1);
    if (between.empty()) return std::string(name);
    for (char c : between) {
        if (c < '0' || c > '9') return std::string(name);
    }

    return std::string(name.substr(0, parenOpen - 1));
}

std::vector<HostileCreatureSpells> HostileSpellCollector::collect(
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    const CombatDatabase* combatDb,
    const EventDatabase* eventDb,
    const ActorColorGenerator* colorGen
) {
    if (!spellCastDb && !auraDb && !combatDb && !eventDb) {
        return {};
    }

    // Track unique spells, debuffs, and damage per creature NAME (not GUID)
    std::unordered_map<std::string, CreatureSpellData> creatureSpellsByName;

    scanSpellCasts(spellCastDb, colorGen, creatureSpellsByName);
    scanDebuffs(auraDb, colorGen, creatureSpellsByName);
    scanBuffs(auraDb, colorGen, creatureSpellsByName);
    scanDamageDealt(combatDb, colorGen, creatureSpellsByName);
    scanActorPresence(eventDb, colorGen, creatureSpellsByName);

    return buildOutput(creatureSpellsByName);
}

void HostileSpellCollector::scanSpellCasts(
    const SpellCastDatabase* spellCastDb,
    const ActorColorGenerator* colorGen,
    std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
) {
    if (!spellCastDb || !colorGen) return;

    for (const auto& [actorGuid, casts] : spellCastDb->getAllActorCasts()) {
        bool isEnvironment = actorGuid == "0000000000000000" ||
                             actorGuid.starts_with("000000");

        // Hostility comes from UnitFlags, as in Actor Appearance Settings
        bool isHostileOrNeutral = false;
        if (!isEnvironment) {
            const UnitFlags* flags = colorGen->getActorFlags(actorGuid);
            isHostileOrNeutral = flags && (flags->isHostile() || flags->isNeutral());
        }

        if (isEnvironment || isHostileOrNeutral) {
            std::string creatureName;
            if (isEnvironment) {
                creatureName = "Environment";
            } else {
                std::string_view name = colorGen->getActorName(actorGuid);
                if (!name.empty()) {
                    creatureName = stripInstanceSuffix(name);
                } else {
                    continue;
                }
            }

            for (const auto& cast : casts) {
                if (cast.spell_id <= 1) continue; // Skip melee (0 and 1)
                creatureSpellsByName[creatureName].spell_ids.insert(cast.spell_id);
                // Track interruptible spells (have cast bar / start timestamp)
                if (cast.start_timestamp_ms > 0) {
                    creatureSpellsByName[creatureName].interruptible_ids.insert(cast.spell_id);
                }
            }
        }
    }
}

void HostileSpellCollector::scanDebuffs(
    const AuraDatabase* auraDb,
    const ActorColorGenerator* colorGen,
    std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
) {
    if (!auraDb || !colorGen) return;

    for (const auto& [targetGuid, instances] : auraDb->getAllTargetAuras()) {
        for (const auto& aura : instances) {
            if (aura.aura_type != AuraType::DEBUFF) continue;
            if (aura.spell_id == 0) continue;

            bool isEnvironment = aura.source_guid == "0000000000000000" ||
                                 aura.source_guid.starts_with("000000") ||
                                 aura.source_guid.empty();

            // Hostility comes from UnitFlags, as in Actor Appearance Settings
            bool isHostileOrNeutral = false;
            if (!isEnvironment) {
                const UnitFlags* flags = colorGen->getActorFlags(aura.source_guid);
                isHostileOrNeutral = flags && (flags->isHostile() || flags->isNeutral());
            }

            // Only debuffs landing on players are of interest
            bool targetIsPlayer = aura.target_guid.starts_with("Player-");

            if ((isEnvironment || isHostileOrNeutral) && targetIsPlayer) {
                std::string creatureName;
                if (isEnvironment) {
                    creatureName = "Environment";
                } else {
                    std::string_view name = colorGen->getActorName(aura.source_guid);
                    if (!name.empty()) {
                        creatureName = stripInstanceSuffix(name);
                    } else {
                        continue;
                    }
                }

                creatureSpellsByName[creatureName].debuff_ids.insert(aura.spell_id);
            }
        }
    }
}

void HostileSpellCollector::scanBuffs(
    const AuraDatabase* auraDb,
    const ActorColorGenerator* colorGen,
    std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
) {
    if (!auraDb || !colorGen) return;

    for (const auto& [targetGuid, instances] : auraDb->getAllTargetAuras()) {
        for (const auto& aura : instances) {
            if (aura.aura_type != AuraType::BUFF) continue;
            if (aura.spell_id == 0) continue;

            bool isEnvironment = aura.source_guid == "0000000000000000" ||
                                 aura.source_guid.starts_with("000000") ||
                                 aura.source_guid.empty();

            // Prefix lookup handles WCL truncated GUIDs
            bool isHostileOrNeutral = false;
            std::string_view actorName;
            if (!isEnvironment) {
                auto [name, flags] = findActorByGuidPrefix(colorGen, aura.source_guid);
                if (flags) {
                    isHostileOrNeutral = flags->isHostile() || flags->isNeutral();
                    actorName = name;
                }
            }

            if (isEnvironment || isHostileOrNeutral) {
                std::string creatureName;
                if (isEnvironment) {
                    creatureName = "Environment";
                } else if (!actorName.empty()) {
                    creatureName = stripInstanceSuffix(actorName);
                } else {
                    continue;
                }

                creatureSpellsByName[creatureName].buff_ids.insert(aura.spell_id);
            }
        }
    }
}

void HostileSpellCollector::scanDamageDealt(
    const CombatDatabase* combatDb,
    const ActorColorGenerator* colorGen,
    std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
) {
    if (!combatDb || !colorGen) return;

    const auto* actorMap = combatDb->getActorMap();
    if (!actorMap) return;

    for (const auto& [actorGuidId, table] : *actorMap) {
        std::string_view actorGuid = guidInterner().lookup(actorGuidId);

        bool isEnvironment = actorGuid == "0000000000000000" ||
                             actorGuid.starts_with("000000");

        bool isHostileOrNeutral = false;
        if (!isEnvironment) {
            const UnitFlags* flags = colorGen->getActorFlags(actorGuid);
            isHostileOrNeutral = flags && (flags->isHostile() || flags->isNeutral());
        }

        if (isEnvironment || isHostileOrNeutral) {
            std::string creatureName;
            if (isEnvironment) {
                creatureName = "Environment";
            } else {
                std::string_view name = colorGen->getActorName(actorGuid);
                if (!name.empty()) {
                    creatureName = stripInstanceSuffix(name);
                } else {
                    continue;
                }
            }

            for (const auto& record : table.damage_dealt_table) {
                if (record.spell_id <= 1) continue; // Skip melee (0 and 1)
                creatureSpellsByName[creatureName].damage_ids.insert(record.spell_id);
            }
        }
    }
}

void HostileSpellCollector::scanActorPresence(
    const EventDatabase* eventDb,
    const ActorColorGenerator* colorGen,
    std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
) {
    if (!eventDb || !colorGen) return;

    // Include every hostile/neutral actor, so actor-based annotations (Follow/Trail/Spawn/Death)
    // work for creatures that never cast or attack
    std::unordered_set<std::string> seenActorGuids;

    eventDb->forEachEvent([&](const ActorEvent& event) {
        std::string guidStr(event.source_guid);
        if (seenActorGuids.count(guidStr)) return;
        seenActorGuids.insert(guidStr);

        if (guidStr == "0000000000000000" ||
            guidStr.starts_with("000000")) return;

        if (!event.flags.isHostile() && !event.flags.isNeutral()) return;

        std::string_view name = colorGen->getActorName(guidStr);
        if (name.empty()) return;
        std::string creatureName = stripInstanceSuffix(name);

        // Creates an empty entry so the Actor category shows for creatures with no spells
        creatureSpellsByName[creatureName];
    });
}

std::vector<HostileCreatureSpells> HostileSpellCollector::buildOutput(
    std::unordered_map<std::string, CreatureSpellData>& creatureSpellsByName
) {
    std::vector<HostileCreatureSpells> result;
    result.reserve(creatureSpellsByName.size());

    // creature_guid stays empty: triggers apply to every actor with this name
    for (auto& [name, data] : creatureSpellsByName) {
        HostileCreatureSpells entry;
        entry.creature_guid = "";
        entry.creature_name = name;
        entry.spell_ids.assign(data.spell_ids.begin(), data.spell_ids.end());
        entry.debuff_ids.assign(data.debuff_ids.begin(), data.debuff_ids.end());
        entry.damage_ids.assign(data.damage_ids.begin(), data.damage_ids.end());
        entry.buff_ids.assign(data.buff_ids.begin(), data.buff_ids.end());
        entry.interruptible_ids = std::move(data.interruptible_ids);
        entry.sortSpells();
        result.push_back(std::move(entry));
    }

    // Sort by creature name (Environment last)
    std::sort(result.begin(), result.end(),
        [](const HostileCreatureSpells& a, const HostileCreatureSpells& b) {
            if (a.creature_name == "Environment" && b.creature_name != "Environment") return false;
            if (a.creature_name != "Environment" && b.creature_name == "Environment") return true;
            return a.creature_name < b.creature_name;
        });

    return result;
}
