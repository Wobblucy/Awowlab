#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>

// Spell GCD/cooldown data
struct SpellTimingData {
    uint32_t gcd_ms = 0;        // Global cooldown in milliseconds (0 = off-GCD)
    uint32_t cooldown_ms = 0;   // Cooldown in milliseconds
    uint32_t cast_time_ms = 0;  // Base cast time in milliseconds (0 = instant)
};

// Loads and provides lookup for spell GCD/cooldown data
// Data can be loaded from CSV or binary file
class SpellGCDDatabase {
public:
    SpellGCDDatabase() = default;
    ~SpellGCDDatabase() = default;

    // Load spell timing data from CSV file
    // Expected format: spell_id,name,gcd_ms,cooldown_ms,cast_time_ms[,class]
    bool loadFromCSV(const std::string& filepath);

    // Load spell timing data from binary file (spell_data_*.bin)
    // Preferred method - binary file contains same data as CSV but loads faster
    bool loadFromBinary(const std::string& filepath);

    // Get timing data for a spell (returns default 1500ms GCD if not found)
    SpellTimingData getSpellTiming(uint32_t spellId) const;

    // Get just the GCD for a spell (convenience method)
    uint32_t getGCD(uint32_t spellId) const;

    // Get just the cooldown for a spell
    uint32_t getCooldown(uint32_t spellId) const;

    // Check if spell is off-GCD (GCD = 0)
    bool isOffGCD(uint32_t spellId) const;

    // Check if a spell has timing data
    bool hasSpellData(uint32_t spellId) const;

    // Get number of loaded spells
    size_t size() const { return spellTimings_.size(); }

    // Default GCD for spells not in database (1.5 seconds)
    static constexpr uint32_t DEFAULT_GCD_MS = 1500;

private:
    std::unordered_map<uint32_t, SpellTimingData> spellTimings_;
};
