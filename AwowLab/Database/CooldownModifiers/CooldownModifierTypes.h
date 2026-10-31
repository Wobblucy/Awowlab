#pragma once
#include <cstdint>

namespace awow {

    // Type of cooldown modification applied by a talent
    enum class ModifierType : uint8_t {
        FlatReduction,        // Reduces CD by X milliseconds (e.g., "Holy Shock -3s")
        PercentReduction,     // Reduces CD by X percent (e.g., "Berserk -20%")
        ResourceBased,        // Reduces CD based on resource spending (e.g., "1s per 20 Rage")
        ChargeBased,          // Adds charges to a spell
        ReplaceSpell          // Talent replaces spell with different version
    };

    // WoW power types for resource-based CD reduction
    enum class PowerType : int8_t {
        Mana = 0,
        Rage = 1,
        Focus = 2,
        Energy = 3,
        ComboPoints = 4,
        Runes = 5,
        RunicPower = 6,
        SoulShards = 7,
        LunarPower = 8,
        HolyPower = 9,
        Alternate = 10,
        Maelstrom = 11,
        Chi = 12,
        Insanity = 13,
        ArcaneCharges = 16,
        Fury = 17,
        Pain = 18,
        Essence = 19
    };

    // Cooldown modifier applied by a talent
    // Loaded from talent_cooldown_modifiers_12.0.txt CSV file
    struct CooldownModifier {
        uint32_t modified_spell_id = 0;     // Which spell's cooldown is modified
        ModifierType modifier_type = ModifierType::FlatReduction;
        uint32_t talent_spell_id = 0;       // Which talent grants this modifier

        // Modifier parameters (union to save memory)
        union {
            struct {
                int32_t reduction_ms;        // Milliseconds to reduce (can be negative for increase)
            } flat;

            struct {
                float percent;              // Percentage reduction (0.0-1.0, e.g., 0.20 = 20% reduction)
            } percent;

            struct {
                PowerType power_type;
                int32_t resource_per_second;  // Resource amount per 1 second of CD reduction
            } resource;

            struct {
                uint8_t additional_charges;  // Number of charges to add
            } charge;

            struct {
                uint32_t new_spell_id;      // Replacement spell ID
            } replace;
        };

        const char* description = nullptr;  // Human-readable description (for debugging)
    };

} // namespace awow
