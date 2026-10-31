#include "CooldownCalculator.h"
#include <algorithm>

namespace awow {

    CooldownCalculator::CooldownCalculator(const TalentDatabase* talentDb,
                                           const CooldownModifierRegistry* modifierRegistry)
        : talent_db_(talentDb), modifier_registry_(modifierRegistry) {
    }

    int32_t CooldownCalculator::calculateCooldown(
        const std::string& player_guid,
        uint32_t spell_id,
        int32_t base_cooldown_ms
    ) const {
        // If no base cooldown, nothing to modify
        if (base_cooldown_ms == 0) {
            return 0;
        }

        const auto* player_talents = talent_db_->getPlayerTalentSpells(player_guid);
        if (!player_talents) {
            return base_cooldown_ms;  // Player not found, return base CD
        }

        auto affecting_talents = modifier_registry_->getTalentsAffectingSpell(spell_id);
        if (affecting_talents.empty()) {
            return base_cooldown_ms;  // No talents affect this spell
        }

        int32_t modified_cd = base_cooldown_ms;

        // Apply all relevant talent modifiers
        // Order: flat reductions first, then percent reductions
        // This matches most WoW talents' behavior

        // Pass 1: Flat reductions
        for (uint32_t talent_spell_id : affecting_talents) {
            if (player_talents->count(talent_spell_id) == 0) {
                continue;  // Player doesn't have this talent
            }

            const auto* modifiers = modifier_registry_->getModifiersForTalent(talent_spell_id);
            if (!modifiers) {
                continue;
            }

            for (const auto& mod : *modifiers) {
                if (mod.modified_spell_id != spell_id) {
                    continue;  // This modifier affects a different spell
                }

                // Apply flat reductions only
                if (mod.modifier_type == ModifierType::FlatReduction) {
                    modified_cd = applyModifier(modified_cd, mod);
                }
            }
        }

        // Pass 2: Percent reductions (applied to result of flat reductions)
        for (uint32_t talent_spell_id : affecting_talents) {
            if (player_talents->count(talent_spell_id) == 0) {
                continue;
            }

            const auto* modifiers = modifier_registry_->getModifiersForTalent(talent_spell_id);
            if (!modifiers) {
                continue;
            }

            for (const auto& mod : *modifiers) {
                if (mod.modified_spell_id != spell_id) {
                    continue;
                }

                // Apply percent reductions
                if (mod.modifier_type == ModifierType::PercentReduction) {
                    modified_cd = applyModifier(modified_cd, mod);
                }
            }
        }

        modified_cd = std::max(0, modified_cd);
        return modified_cd;
    }

    int32_t CooldownCalculator::calculateFinalCooldown(
        const std::string& player_guid,
        uint32_t spell_id,
        int32_t base_cooldown_ms,
        float player_haste_percent,
        bool cd_haste_affected
    ) const {
        int32_t talent_modified_cd = calculateCooldown(player_guid, spell_id, base_cooldown_ms);

        // Haste shortens the cooldown only for spells flagged as haste-affected
        if (cd_haste_affected && player_haste_percent > 0.0f) {
            float haste_factor = 1.0f + player_haste_percent;
            int32_t hasted_cd = static_cast<int32_t>(talent_modified_cd / haste_factor);
            return hasted_cd;
        }

        return talent_modified_cd;
    }

    std::vector<const CooldownModifier*> CooldownCalculator::getActiveModifiers(
        const std::string& player_guid,
        uint32_t spell_id
    ) const {
        std::vector<const CooldownModifier*> active_modifiers;

        const auto* player_talents = talent_db_->getPlayerTalentSpells(player_guid);
        if (!player_talents) {
            return active_modifiers;
        }

        auto affecting_talents = modifier_registry_->getTalentsAffectingSpell(spell_id);

        for (uint32_t talent_spell_id : affecting_talents) {
            if (player_talents->count(talent_spell_id) == 0) {
                continue;
            }

            const auto* modifiers = modifier_registry_->getModifiersForTalent(talent_spell_id);
            if (!modifiers) {
                continue;
            }

            for (const auto& mod : *modifiers) {
                if (mod.modified_spell_id == spell_id) {
                    active_modifiers.push_back(&mod);
                }
            }
        }

        return active_modifiers;
    }

    int32_t CooldownCalculator::applyModifier(int32_t base_cooldown_ms, const CooldownModifier& mod) const {
        switch (mod.modifier_type) {
            case ModifierType::FlatReduction:
                return base_cooldown_ms - mod.flat.reduction_ms;

            case ModifierType::PercentReduction: {
                float reduction = base_cooldown_ms * mod.percent.percent;
                return base_cooldown_ms - static_cast<int32_t>(reduction);
            }

            case ModifierType::ResourceBased:
                // Resource-based modifiers are applied dynamically during gameplay
                // Not applicable to static base cooldown calculation
                return base_cooldown_ms;

            case ModifierType::ChargeBased:
                // Charge-based modifiers don't change the cooldown duration
                // They add charges (handled elsewhere in the system)
                return base_cooldown_ms;

            case ModifierType::ReplaceSpell:
                // Spell replacement is handled at a higher level
                // The replaced spell ID would be looked up instead
                return base_cooldown_ms;

            default:
                return base_cooldown_ms;
        }
    }

} // namespace awow
