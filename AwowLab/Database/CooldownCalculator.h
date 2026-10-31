#pragma once
#include "TalentDatabase.h"
#include "CooldownModifiers/CooldownModifierRegistry.h"
#include <vector>
#include <string>
#include <cstdint>

namespace awow {

    // Calculates talent-modified cooldowns by combining:
    // 1. Player's talents (from TalentDatabase)
    // 2. Talent→CD modifier mappings (from CooldownModifierRegistry)
    // 3. Base cooldown values (from SpellDataTable)
    //
    // Calculation order:
    // 1. Apply talent modifiers (flat and percent)
    // 2. Apply haste scaling (if cd_haste_affected)
    // 3. Apply resource-based reductions (future enhancement, tracked dynamically)
    class CooldownCalculator {
    public:
        CooldownCalculator(const TalentDatabase* talentDb,
                           const CooldownModifierRegistry* modifierRegistry);

        // Calculate talent-modified cooldown for a player's spell
        // Returns modified cooldown in milliseconds
        // If player has no talents affecting this spell, returns base_cooldown_ms unchanged
        int32_t calculateCooldown(
            const std::string& player_guid,
            uint32_t spell_id,
            int32_t base_cooldown_ms
        ) const;

        // Calculate final cooldown with talent modifiers AND haste
        // Order: base → talents → haste
        // Returns final cooldown in milliseconds
        int32_t calculateFinalCooldown(
            const std::string& player_guid,
            uint32_t spell_id,
            int32_t base_cooldown_ms,
            float player_haste_percent,  // e.g., 0.30 for 30% haste
            bool cd_haste_affected
        ) const;

        // Get list of active modifiers for a player's spell
        // Useful for debugging/UI display
        std::vector<const CooldownModifier*> getActiveModifiers(
            const std::string& player_guid,
            uint32_t spell_id
        ) const;

    private:
        const TalentDatabase* talent_db_;
        const CooldownModifierRegistry* modifier_registry_;

        // Apply a single modifier to a cooldown value
        int32_t applyModifier(int32_t base_cooldown_ms, const CooldownModifier& mod) const;
    };

} // namespace awow
