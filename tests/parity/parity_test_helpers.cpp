#include "parity_test_helpers.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <rapidjson/document.h>

namespace parity {

void GuidNormalizer::addMapping(const std::string& local_guid, const std::string& wcl_guid) {
    local_to_wcl_[local_guid] = wcl_guid;
    wcl_to_local_[wcl_guid] = local_guid;
}

void GuidNormalizer::addMappingFromId(int32_t wcl_id, const std::string& local_guid) {
    std::string wcl_guid;

    // The synthetic GUID takes its type prefix from the local GUID.
    if (local_guid.find("Player-") == 0) {
        wcl_guid = "Player-0-0-0-0-" + std::to_string(wcl_id);
    } else if (local_guid.find("Creature-") == 0) {
        wcl_guid = "Creature-0-0-0-0-" + std::to_string(wcl_id);
    } else if (local_guid.find("Pet-") == 0) {
        wcl_guid = "Pet-0-0-0-0-" + std::to_string(wcl_id);
    } else {
        wcl_guid = "Unknown-0-0-0-0-" + std::to_string(wcl_id);
    }

    addMapping(local_guid, wcl_guid);
}

bool GuidNormalizer::areEquivalent(const std::string& guid1, const std::string& guid2) const {
    if (guid1 == guid2) return true;

    auto it1 = local_to_wcl_.find(guid1);
    if (it1 != local_to_wcl_.end() && it1->second == guid2) return true;

    auto it2 = local_to_wcl_.find(guid2);
    if (it2 != local_to_wcl_.end() && it2->second == guid1) return true;

    auto it3 = wcl_to_local_.find(guid1);
    if (it3 != wcl_to_local_.end() && it3->second == guid2) return true;

    auto it4 = wcl_to_local_.find(guid2);
    if (it4 != wcl_to_local_.end() && it4->second == guid1) return true;

    return false;
}

std::string GuidNormalizer::toWclGuid(const std::string& local_guid) const {
    auto it = local_to_wcl_.find(local_guid);
    return (it != local_to_wcl_.end()) ? it->second : local_guid;
}

std::string GuidNormalizer::toLocalGuid(const std::string& wcl_guid) const {
    auto it = wcl_to_local_.find(wcl_guid);
    return (it != wcl_to_local_.end()) ? it->second : wcl_guid;
}

// Melee is spell 0 in local logs and spell 1 in WCL.
bool spellIdsMatch(uint32_t local_spell_id, uint32_t wcl_spell_id) {
    if (local_spell_id == wcl_spell_id) return true;
    if (local_spell_id == 0 && wcl_spell_id == 1) return true;
    if (local_spell_id == 1 && wcl_spell_id == 0) return true;
    return false;
}

ComparisonResult compareCombatEvents(
    const CombatEvent& local,
    const CombatEvent& wcl,
    const ComparisonTolerances& tol
) {
    ComparisonResult result;
    result.matched = true;

    if (!withinTolerance(local.timestamp_ms, wcl.timestamp_ms, tol.timestamp_ms)) {
        result.matched = false;
        result.difference = "Timestamp mismatch: local=" + std::to_string(local.timestamp_ms) +
                           " wcl=" + std::to_string(wcl.timestamp_ms);
        return result;
    }

    if (!spellIdsMatch(local.spell_id, wcl.spell_id)) {
        result.matched = false;
        result.difference = "Spell ID mismatch: local=" + std::to_string(local.spell_id) +
                           " wcl=" + std::to_string(wcl.spell_id);
        return result;
    }

    // Effective damage or healing.
    if (!withinTolerance(local.effective_amount, wcl.effective_amount, tol.amount_percent)) {
        result.matched = false;
        result.difference = "Effective amount mismatch: local=" + std::to_string(local.effective_amount) +
                           " wcl=" + std::to_string(wcl.effective_amount);
        return result;
    }

    if (local.event_type != wcl.event_type) {
        result.matched = false;
        result.difference = "Event type mismatch: local=" + std::to_string(static_cast<int>(local.event_type)) +
                           " wcl=" + std::to_string(static_cast<int>(wcl.event_type));
        return result;
    }

    bool local_crit = (local.flags & CombatEventFlags::Critical) != 0;
    bool wcl_crit = (wcl.flags & CombatEventFlags::Critical) != 0;
    if (local_crit != wcl_crit) {
        result.matched = false;
        result.difference = "Critical flag mismatch: local=" + std::to_string(local_crit) +
                           " wcl=" + std::to_string(wcl_crit);
        return result;
    }

    bool local_periodic = (local.flags & CombatEventFlags::Periodic) != 0;
    bool wcl_periodic = (wcl.flags & CombatEventFlags::Periodic) != 0;
    if (local_periodic != wcl_periodic) {
        result.matched = false;
        result.difference = "Periodic flag mismatch: local=" + std::to_string(local_periodic) +
                           " wcl=" + std::to_string(wcl_periodic);
        return result;
    }

    return result;
}

ComparisonResult compareAuraEvents(
    const AuraEvent& local,
    const AuraEvent& wcl,
    const ComparisonTolerances& tol
) {
    ComparisonResult result;
    result.matched = true;

    if (!withinTolerance(local.timestamp_ms, wcl.timestamp_ms, tol.timestamp_ms)) {
        result.matched = false;
        result.difference = "Timestamp mismatch: local=" + std::to_string(local.timestamp_ms) +
                           " wcl=" + std::to_string(wcl.timestamp_ms);
        return result;
    }

    if (local.spell_id != wcl.spell_id) {
        result.matched = false;
        result.difference = "Spell ID mismatch: local=" + std::to_string(local.spell_id) +
                           " wcl=" + std::to_string(wcl.spell_id);
        return result;
    }

    if (local.aura_type != wcl.aura_type) {
        result.matched = false;
        result.difference = "Aura type mismatch: local=" + std::to_string(static_cast<int>(local.aura_type)) +
                           " wcl=" + std::to_string(static_cast<int>(wcl.aura_type));
        return result;
    }

    if (local.event_type != wcl.event_type) {
        result.matched = false;
        result.difference = "Event type mismatch: local=" + std::to_string(static_cast<int>(local.event_type)) +
                           " wcl=" + std::to_string(static_cast<int>(wcl.event_type));
        return result;
    }

    if (local.stacks != wcl.stacks) {
        result.matched = false;
        result.difference = "Stack count mismatch: local=" + std::to_string(local.stacks) +
                           " wcl=" + std::to_string(wcl.stacks);
        return result;
    }

    return result;
}

ComparisonResult compareSpellCastEvents(
    const SpellCastEvent& local,
    const SpellCastEvent& wcl,
    const ComparisonTolerances& tol
) {
    ComparisonResult result;
    result.matched = true;

    // When the cast completed.
    if (!withinTolerance(local.end_timestamp_ms, wcl.end_timestamp_ms, tol.timestamp_ms)) {
        result.matched = false;
        result.difference = "End timestamp mismatch: local=" + std::to_string(local.end_timestamp_ms) +
                           " wcl=" + std::to_string(wcl.end_timestamp_ms);
        return result;
    }

    if (local.spell_id != wcl.spell_id) {
        result.matched = false;
        result.difference = "Spell ID mismatch: local=" + std::to_string(local.spell_id) +
                           " wcl=" + std::to_string(wcl.spell_id);
        return result;
    }

    if (local.cancelled != wcl.cancelled) {
        result.matched = false;
        result.difference = "Cancelled flag mismatch: local=" + std::to_string(local.cancelled) +
                           " wcl=" + std::to_string(wcl.cancelled);
        return result;
    }

    int32_t local_duration = local.end_timestamp_ms - local.start_timestamp_ms;
    int32_t wcl_duration = wcl.end_timestamp_ms - wcl.start_timestamp_ms;
    if (!withinTolerance(local_duration, wcl_duration, tol.timestamp_ms)) {
        result.matched = false;
        result.difference = "Cast duration mismatch: local=" + std::to_string(local_duration) +
                           " wcl=" + std::to_string(wcl_duration);
        return result;
    }

    return result;
}

ComparisonResult compareDeathEvents(
    const DeathEvent& local,
    const DeathEvent& wcl,
    const ComparisonTolerances& tol
) {
    ComparisonResult result;
    result.matched = true;

    if (!withinTolerance(local.timestamp_ms, wcl.timestamp_ms, tol.timestamp_ms)) {
        result.matched = false;
        result.difference = "Timestamp mismatch: local=" + std::to_string(local.timestamp_ms) +
                           " wcl=" + std::to_string(wcl.timestamp_ms);
        return result;
    }

    // GUIDs are formatted differently, so compare by name.
    if (local.actor_name != wcl.actor_name) {
        result.matched = false;
        result.difference = "Actor name mismatch: local=" + local.actor_name +
                           " wcl=" + wcl.actor_name;
        return result;
    }

    return result;
}

size_t findMatchingCombatEvent(
    const CombatEvent& local,
    const std::vector<CombatEvent>& wcl_events,
    const std::vector<bool>& wcl_matched,
    const ComparisonTolerances& tol
) {
    size_t best_idx = SIZE_MAX;
    int32_t best_diff = INT32_MAX;

    for (size_t i = 0; i < wcl_events.size(); ++i) {
        if (wcl_matched[i]) continue;

        const auto& wcl = wcl_events[i];

        // Spell id (with the melee difference) and event type must match.
        if (!spellIdsMatch(local.spell_id, wcl.spell_id)) continue;
        if (local.event_type != wcl.event_type) continue;

        int32_t diff = std::abs(local.timestamp_ms - wcl.timestamp_ms);
        if (diff > tol.timestamp_ms) continue;

        // Of the candidates, take the closest in time.
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    return best_idx;
}

size_t findMatchingAuraEvent(
    const AuraEvent& local,
    const std::vector<AuraEvent>& wcl_events,
    const std::vector<bool>& wcl_matched,
    const ComparisonTolerances& tol
) {
    size_t best_idx = SIZE_MAX;
    int32_t best_diff = INT32_MAX;

    for (size_t i = 0; i < wcl_events.size(); ++i) {
        if (wcl_matched[i]) continue;

        const auto& wcl = wcl_events[i];

        // Spell id and event type must match.
        if (local.spell_id != wcl.spell_id) continue;
        if (local.event_type != wcl.event_type) continue;

        int32_t diff = std::abs(local.timestamp_ms - wcl.timestamp_ms);
        if (diff > tol.timestamp_ms) continue;

        // Of the candidates, take the closest in time.
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    return best_idx;
}

ParityResult compareExtractedData(
    const ExtractedData& local,
    const ExtractedData& wcl,
    [[maybe_unused]] const GuidNormalizer& guidMap,
    const ComparisonTolerances& tol
) {
    ParityResult result;

    std::vector<bool> wcl_combat_matched(wcl.combatEvents.size(), false);

    for (const auto& local_evt : local.combatEvents) {
        size_t match_idx = findMatchingCombatEvent(local_evt, wcl.combatEvents, wcl_combat_matched, tol);

        if (match_idx != SIZE_MAX) {
            wcl_combat_matched[match_idx] = true;

            auto cmp = compareCombatEvents(local_evt, wcl.combatEvents[match_idx], tol);
            if (cmp.matched) {
                result.combat_events_matched++;
            } else {
                result.combat_events_mismatched++;
                result.differences.push_back("Combat: " + cmp.difference);
                result.passed = false;
            }
        } else {
            result.combat_events_mismatched++;
            result.differences.push_back("Combat: No WCL match for spell_id=" +
                std::to_string(local_evt.spell_id) + " at t=" +
                std::to_string(local_evt.timestamp_ms));
            result.passed = false;
        }
    }

    std::vector<bool> wcl_aura_matched(wcl.auraEvents.size(), false);

    for (const auto& local_evt : local.auraEvents) {
        size_t match_idx = findMatchingAuraEvent(local_evt, wcl.auraEvents, wcl_aura_matched, tol);

        if (match_idx != SIZE_MAX) {
            wcl_aura_matched[match_idx] = true;

            auto cmp = compareAuraEvents(local_evt, wcl.auraEvents[match_idx], tol);
            if (cmp.matched) {
                result.aura_events_matched++;
            } else {
                result.aura_events_mismatched++;
                result.differences.push_back("Aura: " + cmp.difference);
                result.passed = false;
            }
        } else {
            result.aura_events_mismatched++;
            result.differences.push_back("Aura: No WCL match for spell_id=" +
                std::to_string(local_evt.spell_id) + " at t=" +
                std::to_string(local_evt.timestamp_ms));
            result.passed = false;
        }
    }

    // Deaths should match exactly.
    std::vector<bool> wcl_death_matched(wcl.deathEvents.size(), false);

    for (const auto& local_evt : local.deathEvents) {
        bool found = false;

        for (size_t i = 0; i < wcl.deathEvents.size(); ++i) {
            if (wcl_death_matched[i]) continue;

            auto cmp = compareDeathEvents(local_evt, wcl.deathEvents[i], tol);
            if (cmp.matched) {
                wcl_death_matched[i] = true;
                result.death_events_matched++;
                found = true;
                break;
            }
        }

        if (!found) {
            result.death_events_mismatched++;
            result.differences.push_back("Death: No WCL match for " +
                local_evt.actor_name + " at t=" +
                std::to_string(local_evt.timestamp_ms));
            result.passed = false;
        }
    }

    return result;
}

GuidNormalizer loadGuidMapping(const std::string& metadata_path) {
    GuidNormalizer normalizer;

    std::ifstream file(metadata_path);
    if (!file.is_open()) {
        return normalizer;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    rapidjson::Document doc;
    doc.Parse(content.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        return normalizer;
    }

    if (doc.HasMember("actor_mapping") && doc["actor_mapping"].IsObject()) {
        const auto& mapping = doc["actor_mapping"];

        for (auto it = mapping.MemberBegin(); it != mapping.MemberEnd(); ++it) {
            if (!it->value.IsObject()) continue;

            int32_t wcl_id = 0;
            std::string local_guid;

            if (it->value.HasMember("wcl_id") && it->value["wcl_id"].IsInt()) {
                wcl_id = it->value["wcl_id"].GetInt();
            }
            if (it->value.HasMember("local_guid") && it->value["local_guid"].IsString()) {
                local_guid = it->value["local_guid"].GetString();
            }

            if (wcl_id > 0 && !local_guid.empty()) {
                normalizer.addMappingFromId(wcl_id, local_guid);
            }
        }
    }

    return normalizer;
}

} // namespace parity
