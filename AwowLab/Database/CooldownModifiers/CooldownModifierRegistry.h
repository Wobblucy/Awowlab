#pragma once
#include "CooldownModifierTypes.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace awow {

    // Registry mapping talents to their cooldown modifications
    // Loaded from cooldown_modifiers_{version}.json in the data/ folder
    // Singleton pattern for global access
    class CooldownModifierRegistry {
    public:
        // Singleton access
        static CooldownModifierRegistry& instance();

        // Load modifiers from JSON file (user-editable format)
        // Format: see cooldown_modifiers_12.0.json in data/ folder
        bool loadFromJson(const std::string& filepath);

        // Get all modifiers granted by a specific talent spell ID
        const std::vector<CooldownModifier>* getModifiersForTalent(uint32_t talent_spell_id) const;

        // Get all talent spell IDs that affect a specific spell
        std::vector<uint32_t> getTalentsAffectingSpell(uint32_t spell_id) const;

        void clear();

        size_t modifierCount() const { return all_modifiers_.size(); }

    private:
        // Private constructor for singleton
        CooldownModifierRegistry() = default;
        ~CooldownModifierRegistry() = default;

        // Delete copy and move constructors/operators
        CooldownModifierRegistry(const CooldownModifierRegistry&) = delete;
        CooldownModifierRegistry& operator=(const CooldownModifierRegistry&) = delete;
        CooldownModifierRegistry(CooldownModifierRegistry&&) = delete;
        CooldownModifierRegistry& operator=(CooldownModifierRegistry&&) = delete;

        // Register a single modifier
        void registerModifier(const CooldownModifier& modifier);

        // Data structures
        std::unordered_map<uint32_t, std::vector<CooldownModifier>> talent_to_modifiers_;  // talent_spell_id → modifiers
        std::unordered_multimap<uint32_t, uint32_t> spell_to_talents_;                     // modified_spell_id → talent_spell_ids
        std::vector<CooldownModifier> all_modifiers_;                                       // All modifiers (for iteration)
    };

} // namespace awow
