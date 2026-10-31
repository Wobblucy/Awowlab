#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cmath>
#include "../OutputWriter.h"
#include "Haste/HasteTracker.h"
#include "Parser/SpecializationColors.h"
#include "Core/Utils/ColorUtils.h"
#include "Core/StringInterner.h"

namespace awow::extract {

// Shared state for event extraction - passed to all handler functions
struct EventExtractorContext {
    ExtractedData& result;

    // Caches built during extraction
    std::unordered_map<std::string, uint16_t>& specIdCache;      // Player GUID -> SpecID
    std::unordered_map<std::string, ActorColor>& colorCache;     // Any GUID -> Color

    // Cast tracking for pairing START/SUCCESS events
    struct PendingCast {
        uint32_t spell_id;
        int32_t start_timestamp_ms;
    };
    std::unordered_map<std::string, PendingCast>& pendingCastStarts;

    // Haste tracking for GCD/cast bar calculations
    haste::HasteTracker& hasteTracker;

    // Helper function references (bound at construction)
    std::function<void(uint32_t, std::string_view)> registerSpellName;
    std::function<void(std::string_view, std::string_view)> registerGuidName;
};

// Common parsed data shared across event types
struct ParsedEventCommon {
    std::string_view info_guid;
    std::string_view source_guid;
    std::string_view source_name;
    UnitFlags source_flags;
    std::string_view dest_guid;
    std::string_view dest_name;
    UnitFlags dest_flags;
    uint64_t current_hp = 0;
    uint64_t max_hp = 0;

    // Power as reported on this event. Advanced lines carry every resource the
    // spec has, so this is copied straight off AdvancedUnitInfo rather than
    // being flattened to the primary.
    AdvancedUnitInfo::PowerEntry powers[AdvancedUnitInfo::MAX_POWER_TYPES];
    uint8_t power_count = 0;

    bool has_advanced_info = false;
};

// Helper to extract position from AdvancedUnitInfo into global ::UnitRendering
inline void extractPosition(::UnitRendering& ur, const AdvancedUnitInfo& info) {
    if (info.position.valid) {
        ur.x_loc = info.position.x_loc;
        ur.y_loc = info.position.y_loc;
        ur.map_id = info.position.map_id;
        // info.position.facing is already uint16_t (radians * 10000)
        ur.radians = info.position.facing;
    }
}

// Record a resource gain. Both SPELL_ENERGIZE and its periodic variant carry
// the same suffix, so one template covers them.
template<typename T>
void recordEnergize(ExtractedData& result, const T& data) {
    EnergizeEvent ev;
    ev.actor_guid = std::string(data.source_guid);
    ev.timestamp_ms = data.timestamp_ms;
    ev.spell_id = data.spell.spell_id;
    ev.power_type = static_cast<int8_t>(data.power_type);
    ev.amount = data.amount;
    ev.overflow = data.over_energize;

    // Local logs name the spell inline, but nothing was recording it - only
    // the WCL adapter and snapshot importer fill spellNameMap, so an export
    // from a local log came out with bare ids.
    if (ev.spell_id != 0 && !data.spell.spell_name.empty()) {
        result.spellNameMap.try_emplace(ev.spell_id, data.spell.spell_name);
    }

    result.energizeEvents.push_back(std::move(ev));
}

// Helper to populate common struct from any parsed event with AdvancedUnitInfo
template<typename T>
void populateCommon(ParsedEventCommon& common, const T& data,
                    const std::function<void(std::string_view, std::string_view)>& registerGuidName) {
    common.info_guid = data.advanced_info.info_guid;
    common.source_guid = data.source_guid;
    common.source_name = data.source_name;
    common.source_flags = data.source_flags;
    common.dest_guid = data.dest_guid;
    common.dest_name = data.dest_name;
    common.dest_flags = data.dest_flags;
    common.current_hp = data.advanced_info.current_hp;
    common.power_count = data.advanced_info.power_count;
    for (uint8_t i = 0; i < data.advanced_info.power_count; ++i) {
        common.powers[i] = data.advanced_info.powers[i];
    }
    common.max_hp = data.advanced_info.max_hp;
    common.has_advanced_info = data.advanced_info.position.valid;

    // Register GUID->Name for both source and target
    registerGuidName(data.source_guid, data.source_name);
    registerGuidName(data.dest_guid, data.dest_name);
}

// Create CombatEvent with common damage fields populated
// amount is unmitigated (baseAmount); post_mitigation_amount is after armor, absorb, block and resist.
// If is_crit is true, we double the base amount to approximate post-crit value for mitigation calculation
inline CombatEvent createDamageCombatEvent(
    std::string_view source_guid,
    std::string_view dest_guid,
    std::string_view owner_guid,
    int32_t timestamp_ms,
    uint32_t spell_id,
    int64_t amount,
    int64_t post_mitigation_amount,
    int64_t overkill,
    int64_t absorbed,
    int64_t blocked,
    int64_t resisted,
    uint8_t spell_school,
    bool is_crit
) {
    CombatEvent evt;
    evt.source_guid_id = guidInterner().intern(source_guid);
    evt.target_guid_id = guidInterner().intern(dest_guid);
    evt.owner_guid_id = guidInterner().intern(owner_guid);
    evt.timestamp_ms = timestamp_ms;
    evt.spell_id = spell_id;
    // Double base amount for crits to approximate post-crit value
    // This makes (amount - effective_amount) show actual mitigation instead of crit bonus
    evt.amount = is_crit ? (amount * 2) : amount;
    // effective_amount = post-mitigation damage minus overkill
    // This matches WCL's calculation where amount field is already post-mitigation
    evt.effective_amount = post_mitigation_amount - std::max<int64_t>(0, overkill);
    evt.absorbed = absorbed;
    evt.blocked = blocked;
    evt.resisted = resisted;
    evt.spell_school = spell_school;
    evt.event_type = CombatEventType::DamageDealt;
    return evt;
}

// Create CombatEvent with common heal fields populated
inline CombatEvent createHealCombatEvent(
    std::string_view source_guid,
    std::string_view dest_guid,
    std::string_view owner_guid,
    int32_t timestamp_ms,
    uint32_t spell_id,
    int64_t amount,
    int64_t absorbed,
    int64_t overhealing,
    uint8_t spell_school
) {
    CombatEvent evt;
    evt.source_guid_id = guidInterner().intern(source_guid);
    evt.target_guid_id = guidInterner().intern(dest_guid);
    evt.owner_guid_id = guidInterner().intern(owner_guid);
    evt.timestamp_ms = timestamp_ms;
    evt.spell_id = spell_id;
    // WoW heal format: amount = raw/total heal attempted, overhealing = wasted portion
    // absorbed = healing absorbed by debuffs (counts as effective since it was "used")
    // Effective = amount - overhealing (actual HP restored + absorbed)
    evt.amount = amount;
    evt.effective_amount = amount - overhealing;
    evt.absorbed = absorbed;
    evt.blocked = 0;
    evt.resisted = 0;
    evt.spell_school = spell_school;
    evt.event_type = CombatEventType::HealingDone;
    return evt;
}

inline uint16_t buildDamageFlags(
    bool critical, int64_t overkill, int64_t absorbed,
    int64_t blocked, int64_t resisted, bool glancing,
    bool crushing, bool is_off_hand, bool is_periodic = false,
    bool is_swing = false, bool is_support = false
) {
    uint16_t flags = 0;
    if (is_periodic) flags |= CombatEventFlags::Periodic;
    if (is_swing) flags |= CombatEventFlags::IsSwing;
    if (is_support) flags |= CombatEventFlags::Support;
    if (critical) flags |= CombatEventFlags::Critical;
    if (overkill > 0) flags |= CombatEventFlags::Overkill;
    if (absorbed > 0) flags |= CombatEventFlags::Absorbed;
    if (blocked > 0) flags |= CombatEventFlags::Blocked;
    if (resisted > 0) flags |= CombatEventFlags::Resisted;
    if (glancing) flags |= CombatEventFlags::Glancing;
    if (crushing) flags |= CombatEventFlags::Crushing;
    if (is_off_hand) flags |= CombatEventFlags::OffHand;
    return flags;
}

inline uint16_t buildHealFlags(
    bool critical, int64_t overhealing, int64_t absorbed,
    bool is_periodic = false, bool is_support = false
) {
    uint16_t flags = 0;
    if (is_periodic) flags |= CombatEventFlags::Periodic;
    if (is_support) flags |= CombatEventFlags::Support;
    if (critical) flags |= CombatEventFlags::Critical;
    if (overhealing > 0) flags |= CombatEventFlags::Overheal;
    if (absorbed > 0) flags |= CombatEventFlags::Absorbed;
    return flags;
}

} // namespace awow::extract
