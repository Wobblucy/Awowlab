#include "../CombatDatabase.h"
#include "QueryHelpers.h"
#include "Core/StringInterner.h"
#include "Core/MobWeightSettings.h"
#include "Structures/CombatTypes.h"  // For CombatEventFlags
#include <algorithm>
#include <unordered_map>

void CombatDatabase::buildDamageTakenIndex() {
    damageTakenIndex_.clear();
    if (!actorMap_) return;

    // Damage taken is the damage-dealt tables regrouped by target. The index
    // holds pointers into the ActorMap rather than copies.
    for (const auto& [source_guid, table] : *actorMap_) {
        for (const auto& record : table.damage_dealt_table) {
            if (record.target_guid_id != 0) {
                damageTakenIndex_[record.target_guid_id].push_back(&record);
            }
        }
    }

    for (auto& [target_guid_id, records] : damageTakenIndex_) {
        std::sort(records.begin(), records.end(),
            [](const CombatRecord* a, const CombatRecord* b) {
                return a->timestamp_ms < b->timestamp_ms;
            });
    }
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByDamageTaken(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    if (damageTakenIndex_.empty()) {
        return {};
    }

    std::vector<ActorCombatStats> results;
    results.reserve(damageTakenIndex_.size());

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    for (const auto& [target_guid_id, recordPtrs] : damageTakenIndex_) {
        std::string_view target_guid_sv = guidInterner().lookup(target_guid_id);
        std::string target_guid(target_guid_sv);

        ActorCombatStats stats;
        stats.actor_guid = target_guid;

        for (const CombatRecord* recordPtr : recordPtrs) {
            if (recordPtr->timestamp_ms < start_time_ms || recordPtr->timestamp_ms > end_time_ms) {
                continue;
            }

            stats.total_amount += recordPtr->amount;
            stats.effective_amount += recordPtr->effective_amount;
            stats.hit_count++;
            if (recordPtr->flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
        }

        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    // Ranked by effective amount, with percentages taken from the total
    combat_db::sortByEffectiveAmount(results);
    combat_db::calculatePercentagesByTotal(results);
    combat_db::limitResults(results, max_results);
    return results;
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByDamageTakenWithPets(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results,
    bool includeBreakdowns
) const {
    if (damageTakenIndex_.empty()) {
        return {};
    }

    std::unordered_map<StringInterner::Id, ActorCombatStats> aggregatedStats;

    // actor guid id -> spell_id -> stats
    std::unordered_map<StringInterner::Id, std::unordered_map<uint32_t, SpellCombatStats>> spellBreakdowns;

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    for (const auto& [target_guid_id, recordPtrs] : damageTakenIndex_) {
        // Unlike damage done, a pet's damage taken stays on the pet and is not
        // credited to its owner.
        StringInterner::Id effective_owner = target_guid_id;

        auto& ownerEntry = aggregatedStats[effective_owner];
        if (ownerEntry.actor_guid.empty()) {
            ownerEntry.actor_guid = std::string(guidInterner().lookup(effective_owner));
        }

        for (const CombatRecord* recordPtr : recordPtrs) {
            if (recordPtr->timestamp_ms < start_time_ms || recordPtr->timestamp_ms > end_time_ms) {
                continue;
            }

            ownerEntry.total_amount += recordPtr->amount;
            ownerEntry.effective_amount += recordPtr->effective_amount;
            ownerEntry.hit_count++;
            if (recordPtr->flags & CombatEventFlags::Critical) {
                ownerEntry.crit_count++;
            }

            if (includeBreakdowns) {
                auto& spellStats = spellBreakdowns[effective_owner][recordPtr->spell_id];
                spellStats.spell_id = recordPtr->spell_id;
                spellStats.total_amount += recordPtr->amount;
                spellStats.effective_amount += recordPtr->effective_amount;
                spellStats.hit_count++;
                spellStats.spell_school = recordPtr->spell_school;

                if (recordPtr->flags & CombatEventFlags::Critical) {
                    spellStats.crit_count++;
                    spellStats.crit_total += recordPtr->effective_amount;
                    if (recordPtr->effective_amount < spellStats.crit_min) {
                        spellStats.crit_min = recordPtr->effective_amount;
                    }
                    if (recordPtr->effective_amount > spellStats.crit_max) {
                        spellStats.crit_max = recordPtr->effective_amount;
                    }
                } else {
                    spellStats.normal_count++;
                    spellStats.normal_total += recordPtr->effective_amount;
                    if (recordPtr->effective_amount < spellStats.normal_min) {
                        spellStats.normal_min = recordPtr->effective_amount;
                    }
                    if (recordPtr->effective_amount > spellStats.normal_max) {
                        spellStats.normal_max = recordPtr->effective_amount;
                    }
                }

                if (recordPtr->flags & CombatEventFlags::Periodic) {
                    spellStats.periodic_count++;
                }

                if (recordPtr->effective_amount < spellStats.min_hit) {
                    spellStats.min_hit = recordPtr->effective_amount;
                }
                if (recordPtr->effective_amount > spellStats.max_hit) {
                    spellStats.max_hit = recordPtr->effective_amount;
                }
            }
        }
    }

    std::vector<ActorCombatStats> results;
    results.reserve(aggregatedStats.size());
    for (auto& [guid, stats] : aggregatedStats) {
        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;

            if (includeBreakdowns) {
                auto breakdownIt = spellBreakdowns.find(guid);
                if (breakdownIt != spellBreakdowns.end()) {
                    for (auto& [spell_id, spellStats] : breakdownIt->second) {
                        stats.spell_breakdown.push_back(std::move(spellStats));
                    }
                    std::sort(stats.spell_breakdown.begin(), stats.spell_breakdown.end(),
                        [](const SpellCombatStats& a, const SpellCombatStats& b) {
                            return a.total_amount > b.total_amount;
                        });
                }
            }

            results.push_back(std::move(stats));
        }
    }

    // Ranked by effective amount, with percentages taken from the total
    combat_db::sortByEffectiveAmount(results);
    combat_db::calculatePercentagesByTotal(results);
    combat_db::limitResults(results, max_results);
    return results;
}

void CombatDatabase::buildHealingReceivedIndex() {
    healingReceivedIndex_.clear();
    if (!actorMap_) return;

    // Same layout as buildDamageTakenIndex, over the healing-done tables
    for (const auto& [source_guid, table] : *actorMap_) {
        for (const auto& record : table.healing_done_table) {
            if (record.target_guid_id != 0) {
                healingReceivedIndex_[record.target_guid_id].push_back(&record);
            }
        }
    }

    for (auto& [target_guid_id, records] : healingReceivedIndex_) {
        std::sort(records.begin(), records.end(),
            [](const CombatRecord* a, const CombatRecord* b) {
                return a->timestamp_ms < b->timestamp_ms;
            });
    }
}

std::vector<TargetCombatEvent> CombatDatabase::getEventsForTarget(
    const std::string& target_guid,
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_events
) const {
    std::vector<TargetCombatEvent> events;

    // find() never adds to the pool; an unknown guid means no recorded events.
    StringInterner::Id target_guid_id = guidInterner().find(target_guid);
    if (target_guid_id == StringInterner::INVALID) {
        return events;
    }

    auto damageIt = damageTakenIndex_.find(target_guid_id);
    if (damageIt != damageTakenIndex_.end()) {
        for (const CombatRecord* recordPtr : damageIt->second) {
            if (recordPtr->timestamp_ms < start_time_ms || recordPtr->timestamp_ms > end_time_ms) {
                continue;
            }

            TargetCombatEvent event;
            event.timestamp_ms = recordPtr->timestamp_ms;
            event.spell_id = recordPtr->spell_id;
            event.amount = recordPtr->amount;
            event.effective_amount = recordPtr->effective_amount;
            event.is_damage = true;
            event.is_crit = (recordPtr->flags & CombatEventFlags::Critical) != 0;
            event.is_periodic = (recordPtr->flags & CombatEventFlags::Periodic) != 0;
            event.spell_school = recordPtr->spell_school;

            // CombatRecord carries no source guid, so the event has none
            events.push_back(std::move(event));
        }
    }

    auto healingIt = healingReceivedIndex_.find(target_guid_id);
    if (healingIt != healingReceivedIndex_.end()) {
        for (const CombatRecord* recordPtr : healingIt->second) {
            if (recordPtr->timestamp_ms < start_time_ms || recordPtr->timestamp_ms > end_time_ms) {
                continue;
            }

            TargetCombatEvent event;
            event.timestamp_ms = recordPtr->timestamp_ms;
            event.spell_id = recordPtr->spell_id;
            event.amount = recordPtr->amount;
            event.effective_amount = recordPtr->effective_amount;
            event.is_damage = false;
            event.is_crit = (recordPtr->flags & CombatEventFlags::Critical) != 0;
            event.is_periodic = (recordPtr->flags & CombatEventFlags::Periodic) != 0;
            event.spell_school = recordPtr->spell_school;

            events.push_back(std::move(event));
        }
    }

    // Most recent first
    std::sort(events.begin(), events.end(),
        [](const TargetCombatEvent& a, const TargetCombatEvent& b) {
            return a.timestamp_ms > b.timestamp_ms;
        });

    // max_events of 0 means no limit
    if (max_events > 0 && events.size() > max_events) {
        events.resize(max_events);
    }

    return events;
}

std::vector<ActorCombatStats> CombatDatabase::getDamageDoneToTarget(
    const std::string& target_guid,
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    // The picker merges all spawns of one enemy type into a single row, so
    // resolve the clicked guid's npc id and let the group overload gather
    // every spawn of that type. An npc id of 0 (environment, a mob with no
    // npc id in its guid) falls back to matching just this one guid.
    uint32_t npcId = MobWeightSettings::npcIdFromGuid(target_guid);
    return getDamageDoneToTargetGroup(target_guid, npcId, start_time_ms, end_time_ms, max_results);
}

std::vector<ActorCombatStats> CombatDatabase::getDamageDoneToTargetGroup(
    const std::string& target_guid,
    uint32_t target_npc_id,
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    if (!actorMap_ || actorMap_->empty()) {
        return {};
    }

    // Only needed when matching a single guid (no npc-id group). find()
    // never adds to the pool; an unknown guid means nobody hit it.
    StringInterner::Id target_guid_id = guidInterner().find(target_guid);
    if (target_npc_id == 0 && target_guid_id == StringInterner::INVALID) {
        return {};
    }

    std::unordered_map<StringInterner::Id, ActorCombatStats> sourceStats;

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    for (const auto& [source_guid, actor_table] : *actorMap_) {
        for (const auto& record : actor_table.damage_dealt_table) {
            // Keep records aimed at our target. When a group is selected
            // (npc id != 0), match every spawn sharing that npc id so the
            // drill-down covers all copies of the enemy type; otherwise
            // match the one exact guid.
            if (target_npc_id != 0) {
                if (MobWeightSettings::npcIdFromGuid(
                        guidInterner().lookup(record.target_guid_id)) != target_npc_id) {
                    continue;
                }
            } else if (record.target_guid_id != target_guid_id) {
                continue;
            }

            if (record.timestamp_ms < start_time_ms || record.timestamp_ms > end_time_ms) {
                continue;
            }

            // Pets are merged into their owner
            StringInterner::Id effective_source = source_guid;
            auto ownerIt = petToOwnerMap_.find(source_guid);
            if (ownerIt != petToOwnerMap_.end() && ownerIt->second != StringInterner::INVALID) {
                effective_source = ownerIt->second;
            }

            auto& stats = sourceStats[effective_source];
            if (stats.actor_guid.empty()) {
                stats.actor_guid = std::string(guidInterner().lookup(effective_source));
            }

            stats.total_amount += record.amount;
            stats.effective_amount += record.effective_amount;
            stats.hit_count++;
            if (record.flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
        }
    }

    std::vector<ActorCombatStats> results;
    results.reserve(sourceStats.size());
    for (auto& [guid, stats] : sourceStats) {
        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    combat_db::finalizeResults(results, max_results);
    return results;
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByFriendlyFire(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    if (!actorMap_ || actorMap_->empty()) {
        return {};
    }

    std::unordered_map<StringInterner::Id, ActorCombatStats> sourceStats;

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    // Count the records the extractor flagged as friendly fire, the same
    // filter the drill-down uses. Testing "target is a player" instead would
    // rank every boss that hits the raid.
    for (const auto& [source_guid, actor_table] : *actorMap_) {
        for (const auto& record : actor_table.damage_dealt_table) {
            if (record.timestamp_ms < start_time_ms || record.timestamp_ms > end_time_ms) {
                continue;
            }

            if (!(record.flags & CombatEventFlags::FriendlyFire)) {
                continue;
            }

            // Pets are merged into their owner
            StringInterner::Id effective_source = source_guid;
            auto ownerIt = petToOwnerMap_.find(source_guid);
            if (ownerIt != petToOwnerMap_.end() && ownerIt->second != StringInterner::INVALID) {
                effective_source = ownerIt->second;
            }

            auto& stats = sourceStats[effective_source];
            if (stats.actor_guid.empty()) {
                stats.actor_guid = std::string(guidInterner().lookup(effective_source));
            }

            stats.total_amount += record.amount;
            stats.effective_amount += record.effective_amount;
            stats.hit_count++;
            if (record.flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
        }
    }

    std::vector<ActorCombatStats> results;
    results.reserve(sourceStats.size());
    for (auto& [guid, stats] : sourceStats) {
        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    combat_db::finalizeResults(results, max_results);
    return results;
}

// Ranks non-player sources by damage or healing done. Shared by
// getRankedByEnemyDamage and getRankedByEnemyHealing.
static std::vector<ActorCombatStats> rankHostileSources(
    const ActorMap* actorMap,
    const std::unordered_map<StringInterner::Id, StringInterner::Id>& petToOwnerMap,
    int32_t start_time_ms,
    int32_t end_time_ms,
    int32_t min_ts,
    int32_t max_ts,
    size_t max_results,
    CombatMetricType type) {

    if (!actorMap || actorMap->empty()) {
        return {};
    }

    // Keyed by uint64: high bit tags whether the key is an npc id (a merged
    // enemy type) or a lone interned guid, so a guid id can't collide with
    // an npc id that happens to share the same integer value.
    std::unordered_map<uint64_t, ActorCombatStats> sourceStats;
    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_ts, max_ts);

    for (const auto& [source_guid, actor_table] : *actorMap) {
        std::string_view source_guid_sv = guidInterner().lookup(source_guid);
        if (source_guid_sv.starts_with("Player-")) {
            continue;
        }

        // Fold enemy summons (adds' totems, boss-spawned pets) into the
        // actor that owns them so the Enemy Damage list shows one row per
        // real combatant. This mirrors getRankedByActorWithPets, so the
        // guid on each row matches the guid the breakdown panel opens.
        StringInterner::Id effective_source = source_guid;
        auto ownerIt = petToOwnerMap.find(source_guid);
        if (ownerIt != petToOwnerMap.end() && ownerIt->second != StringInterner::INVALID) {
            effective_source = ownerIt->second;
        }

        // A non-player creature can be a player's summon (a hunter pet, a
        // DK ghoul). Folding it into its owner would put that player in the
        // enemy list, so drop it - a player-owned pet is not an enemy source.
        std::string_view effective_source_sv = guidInterner().lookup(effective_source);
        if (effective_source_sv.starts_with("Player-")) {
            continue;
        }

        // WoW spawns many copies of the same add, each a distinct guid, so
        // group spawns of one enemy type by NPC id (the 6th dash field of
        // the guid) into a single row. This mirrors aggregatePetBreakdown's
        // grouping. A creature with no npc id (rare) keeps its own guid so
        // distinct nameless sources aren't collapsed together. The first
        // guid seen for a group is kept as the representative so name
        // resolution and the breakdown drill-down still work.
        uint32_t npcId = MobWeightSettings::npcIdFromGuid(effective_source_sv);
        uint64_t group_key = (npcId != 0)
            ? (uint64_t{1} << 32 | npcId)
            : (uint64_t{2} << 32 | effective_source);

        const auto* combat_table = (type == CombatMetricType::HealingDone)
            ? &actor_table.healing_done_table
            : &actor_table.damage_dealt_table;
        if (combat_table->empty()) continue;

        for (const auto& record : *combat_table) {
            if (record.timestamp_ms < start_time_ms || record.timestamp_ms > end_time_ms) {
                continue;
            }
            auto& stats = sourceStats[group_key];
            if (stats.actor_guid.empty()) {
                stats.actor_guid = std::string(effective_source_sv);
            }
            stats.total_amount += record.amount;
            stats.effective_amount += record.effective_amount;
            stats.hit_count++;
            if (record.flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
        }
    }

    std::vector<ActorCombatStats> results;
    results.reserve(sourceStats.size());
    for (auto& [guid, stats] : sourceStats) {
        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    combat_db::finalizeResults(results, max_results);
    return results;
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByEnemyDamage(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    return rankHostileSources(actorMap_, petToOwnerMap_, start_time_ms, end_time_ms,
                              min_timestamp_, max_timestamp_,
                              max_results, CombatMetricType::DamageDealt);
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByEnemyHealing(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    return rankHostileSources(actorMap_, petToOwnerMap_, start_time_ms, end_time_ms,
                              min_timestamp_, max_timestamp_,
                              max_results, CombatMetricType::HealingDone);
}
