#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

struct ExtractedData;

namespace awow::analysis {

// Spell id to name, resolved by the caller. Local logs name spells inline but
// WCL events carry bare ids, so the caller fills the gaps from the shipped
// spell database and hands the result in. Anything missing from here is left
// blank rather than written as a placeholder.
using SpellNames = std::unordered_map<uint32_t, std::string>;

// The rest of the AI analysis package tables. Same rules as the resource
// writers in ResourceCsvWriter.h: header row, one event per row, timestamps in
// ms relative to the pull, and full GUIDs rather than interned ids so a row
// stands on its own. These compile into release builds - the package is
// something users export, not a debugging aid like DebugDatabaseDumper.
//
// Every writer takes the GUID of the player the package is about. Passing it
// keeps that player, their pets, and what happened to them; an empty GUID
// writes every actor, which is what most tests use. The per-file comment says
// what "scoped" means for that table, because it differs: casts.csv is about
// what the player did, while combat.csv also has to keep damage taken.

// What the player cast: timing, target, haste and cooldown state.
bool writeCastsCsv(const std::string& path,
                   const ExtractedData& data,
                   std::string_view playerGuid = {},
                   const SpellNames* names = nullptr);

// Damage and healing done by the player and their pets, plus damage taken by
// the player. Pets are matched through owner_guid_id, so a hunter's pet damage
// stays attached to the hunter.
bool writeCombatCsv(const std::string& path,
                    const ExtractedData& data,
                    std::string_view playerGuid = {},
                    const SpellNames* names = nullptr);

// Buffs on the player and debuffs the player applied, with stack counts.
bool writeAurasCsv(const std::string& path,
                   const ExtractedData& data,
                   std::string_view playerGuid = {},
                   const SpellNames* names = nullptr);

// Player and enemy position and facing over time.
bool writePositionsCsv(const std::string& path,
                       const ExtractedData& data,
                       std::string_view playerGuid = {});

// Deaths and resurrections, both in one table with an event_type column -
// reading a death without the rez that followed it tells half the story.
bool writeDeathsCsv(const std::string& path,
                    const ExtractedData& data,
                    std::string_view playerGuid = {},
                    const SpellNames* names = nullptr);

// Misses, absorbs and shields on the player. Two event shapes in one table,
// distinguished by event_type, since both answer "what did not land on me".
bool writeDefenseCsv(const std::string& path,
                     const ExtractedData& data,
                     std::string_view playerGuid = {},
                     const SpellNames* names = nullptr);

// Interrupts and dispels by the player.
bool writeUtilityCsv(const std::string& path,
                     const ExtractedData& data,
                     std::string_view playerGuid = {},
                     const SpellNames* names = nullptr);

// Pets and guardians the player summoned.
bool writeSummonsCsv(const std::string& path,
                     const ExtractedData& data,
                     std::string_view playerGuid = {},
                     const SpellNames* names = nullptr);

// Resource-based cooldown reductions.
bool writeCdReductionsCsv(const std::string& path,
                          const ExtractedData& data,
                          std::string_view playerGuid = {},
                          const SpellNames* names = nullptr);

// Spec, talents and haste for the target player. JSON rather than CSV because
// the talent list is nested.
bool writeCombatantJson(const std::string& path,
                        const ExtractedData& data,
                        std::string_view playerGuid = {});

// Spell and actor names for every id used elsewhere in the package, so a reader
// never has to guess what 433895 was.
bool writeLookupsJson(const std::string& path, const ExtractedData& data,
                      const SpellNames* names = nullptr);

} // namespace awow::analysis
