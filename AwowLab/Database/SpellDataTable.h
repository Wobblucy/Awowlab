#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace awow {

    // Spell cooldown/GCD data loaded from binary file
    struct SpellInfo {
        uint32_t spell_id = 0;
        std::string name;
        int32_t gcd_ms = 0;
        int32_t cooldown_ms = 0;
        int32_t cast_time_ms = 0;
        std::string spell_class;
        bool gcd_haste_affected = true;
        bool cd_haste_affected = false;
        uint8_t base_charges = 0;  // From SpellCategory table (0 = not charge-based)
    };

    // Loads and queries spell CD/GCD/haste data
    class SpellDataTable {
    public:
        SpellDataTable() = default;

        // Load all spell data from binary file
        // Format: [uint32_t count] [SpellDataRecord...]
        bool loadFromBinary(const std::string& filepath);

        // Load only relevant spells from binary file (on-demand loading)
        // Only loads spells in the spell_ids set (for current combat log)
        bool loadRelevantSpells(const std::string& filepath, const std::unordered_set<uint32_t>& spell_ids);

        // Get spell info by ID
        // Returns nullptr if spell not found
        const SpellInfo* getSpellInfo(uint32_t spell_id) const;

        // Get base cooldown for a spell (0 if not found or no cooldown)
        int32_t getBaseCooldown(uint32_t spell_id) const;

        // Get base GCD for a spell (0 if off-GCD or not found)
        int32_t getBaseGCD(uint32_t spell_id) const;

        bool isCooldownHasteAffected(uint32_t spell_id) const;

        // Get base charges for a spell (0 if not charge-based or not found)
        uint8_t getBaseCharges(uint32_t spell_id) const;

        size_t size() const { return spells_.size(); }

    private:
        std::unordered_map<uint32_t, SpellInfo> spells_;
    };

} // namespace awow
