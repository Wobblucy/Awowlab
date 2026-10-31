#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include "Parser/WowClass.h"

namespace spell_assignment {

// Re-export parser::WowClass for convenience in UI code
using WowClass = parser::WowClass;

// Use parser:: namespace for class-related functions
using parser::getClassFromSpecId;
using parser::getWowClassName;

// Display locations where spells can appear (bitmask)
enum class SpellDisplayLocation : uint16_t {
    None              = 0,
    CDGrouping1       = 1 << 0,  // Raid CD (Major) panel
    RaidFrameAura     = 1 << 1,  // Icon on player's raid frame
    PlayerHUDBigCD    = 1 << 2,  // HUD (Major)
    GCDTracker        = 1 << 3,  // GCD history panel
    Nameplate         = 1 << 4,  // Icon above actor's 3D nameplate
    PlayerBuffs       = 1 << 5,  // Player Buffs panel (focused actor)
    PlayerDebuffs     = 1 << 6,  // Player Debuffs panel (focused actor)
    All               = 0x7F
};

// Bitwise operators for SpellDisplayLocation
inline SpellDisplayLocation operator|(SpellDisplayLocation a, SpellDisplayLocation b) {
    return static_cast<SpellDisplayLocation>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b)
    );
}

inline SpellDisplayLocation operator&(SpellDisplayLocation a, SpellDisplayLocation b) {
    return static_cast<SpellDisplayLocation>(
        static_cast<uint16_t>(a) & static_cast<uint16_t>(b)
    );
}

inline SpellDisplayLocation& operator|=(SpellDisplayLocation& a, SpellDisplayLocation b) {
    a = a | b;
    return a;
}

inline SpellDisplayLocation& operator&=(SpellDisplayLocation& a, SpellDisplayLocation b) {
    a = a & b;
    return a;
}

inline bool hasFlag(uint16_t mask, SpellDisplayLocation loc) {
    return (mask & static_cast<uint16_t>(loc)) != 0;
}

// Get display name for a location
const char* getLocationName(SpellDisplayLocation location);

// Get short column header for location (for compact table display)
const char* getLocationShortName(SpellDisplayLocation location);

// Per-spell assignment storing which locations it appears in
struct SpellAssignment {
    uint32_t spell_id = 0;
    uint16_t locations = 0;  // Bitmask of SpellDisplayLocation

    bool hasLocation(SpellDisplayLocation loc) const {
        return hasFlag(locations, loc);
    }

    void addLocation(SpellDisplayLocation loc) {
        locations |= static_cast<uint16_t>(loc);
    }

    void removeLocation(SpellDisplayLocation loc) {
        locations &= ~static_cast<uint16_t>(loc);
    }

    void setLocations(uint16_t mask) {
        locations = mask;
    }
};

// Per-aura assignment storing which locations it appears in (for buffs/debuffs)
struct AuraAssignment {
    uint32_t spell_id = 0;
    uint16_t locations = 0;  // Bitmask of SpellDisplayLocation
    bool is_buff = true;     // true = BUFF, false = DEBUFF

    bool hasLocation(SpellDisplayLocation loc) const {
        return hasFlag(locations, loc);
    }

    void addLocation(SpellDisplayLocation loc) {
        locations |= static_cast<uint16_t>(loc);
    }

    void removeLocation(SpellDisplayLocation loc) {
        locations &= ~static_cast<uint16_t>(loc);
    }

    void setLocations(uint16_t mask) {
        locations = mask;
    }
};

// All spell assignments for a single spec
struct SpecSpellAssignments {
    uint16_t specId = 0;
    std::unordered_map<uint32_t, SpellAssignment> spellAssignments;
    std::unordered_map<uint32_t, AuraAssignment> auraAssignments;

    // GCD-only exclusions: spells excluded from GCD tracker but with no other location assignment
    // This handles the case where user unchecks GCD but doesn't assign to any other location
    std::unordered_set<uint32_t> gcdOnlyExclusions;

    // Player Buffs/Debuffs exclusions: auras excluded from respective panels (show-by-default)
    std::unordered_set<uint32_t> playerBuffExclusions;
    std::unordered_set<uint32_t> playerDebuffExclusions;

    // Spell (cooldown) methods
    void setSpellLocations(uint32_t spell_id, uint16_t locationMask);
    uint16_t getSpellLocations(uint32_t spell_id) const;
    bool hasSpellAssignment(uint32_t spell_id) const;
    void removeSpellAssignment(uint32_t spell_id);
    std::vector<uint32_t> getSpellsForLocation(SpellDisplayLocation loc) const;

    // GCD exclusion methods
    void setGCDExcluded(uint32_t spell_id, bool excluded);
    bool isGCDExcluded(uint32_t spell_id) const;

    // Aura (buff/debuff) methods
    void setAuraLocations(uint32_t spell_id, uint16_t locationMask, bool is_buff);
    uint16_t getAuraLocations(uint32_t spell_id) const;
    bool hasAuraAssignment(uint32_t spell_id) const;
    void removeAuraAssignment(uint32_t spell_id);
    std::vector<uint32_t> getAurasForLocation(SpellDisplayLocation loc) const;
};

// Main settings class - manages all spec-based spell assignments
// Persisted via UnifiedSettings and SettingsCache
class SpellAssignmentSettings {
public:
    SpellAssignmentSettings();

    // Query methods for spells (cooldowns) - by spec ID
    uint16_t getLocationsForSpell(uint16_t specId, uint32_t spell_id) const;
    bool shouldShowInLocation(uint16_t specId, uint32_t spell_id, SpellDisplayLocation loc) const;
    std::vector<uint32_t> getSpellsForLocation(uint16_t specId, SpellDisplayLocation loc) const;

    // Modification methods for spells (cooldowns) - by spec ID
    void setSpellLocations(uint16_t specId, uint32_t spell_id, uint16_t locationMask);
    void clearAllForSpec(uint16_t specId);

    // Query methods for auras (buffs/debuffs) - by spec ID
    uint16_t getLocationsForAura(uint16_t specId, uint32_t spell_id) const;
    bool shouldShowAuraInLocation(uint16_t specId, uint32_t spell_id, SpellDisplayLocation loc) const;
    std::vector<uint32_t> getAurasForLocation(uint16_t specId, SpellDisplayLocation loc) const;

    // Modification methods for auras (buffs/debuffs) - by spec ID
    void setAuraLocations(uint16_t specId, uint32_t spell_id, uint16_t locationMask, bool is_buff);
    void clearAuraAssignment(uint16_t specId, uint32_t spell_id);

    // Reset to defaults
    void resetToDefaults(uint16_t specId);
    void resetAllToDefaults();

    // Get or create assignments for a spec
    SpecSpellAssignments& getOrCreateSpecAssignments(uint16_t specId);
    const SpecSpellAssignments* getSpecAssignments(uint16_t specId) const;

    // Serialization
    std::string serializeToJson() const;
    bool deserializeFromJson(const std::string& json);

    // Load/save via SettingsCache
    void loadFromSettings();
    void saveToSettings();

    // Check if using defaults (no user customization)
    bool isUsingDefaults() const { return usingDefaults_; }

    // Fast exclusion check for GCD Tracker (assumes show-by-default)
    // Returns true if the spell should be hidden from the GCD tracker
    // Caches exclusions for fast per-frame lookups
    bool isSpellExcludedFromGCDTracker(uint16_t specId, uint32_t spell_id);

    // GCD exclusion management (for UI toggle)
    // Use this when user toggles GCD checkbox - handles both regular assignments and GCD-only exclusions
    void setSpellGCDExcluded(uint16_t specId, uint32_t spell_id, bool excluded);
    bool isSpellGCDExcluded(uint16_t specId, uint32_t spell_id) const;

    // Invalidate exclusion cache (called when settings change)
    void invalidateExclusionCache() { gcdCacheDirty_ = true; }

    // Fast inclusion check for Nameplate auras (assumes hide-by-default)
    // Returns true if the aura should appear on nameplates
    // Caches inclusions for fast per-frame lookups
    bool isAuraEnabledForNameplate(uint32_t spell_id);

    // Invalidate nameplate cache (called when settings change)
    void invalidateNameplateCache() { nameplateCacheDirty_ = true; }

    // Player Buffs/Debuffs exclusion management (show-by-default, user blacklists)
    // Returns true if the aura should be hidden from the panel
    bool isAuraExcludedFromPlayerBuffs(uint16_t specId, uint32_t spell_id) const;
    bool isAuraExcludedFromPlayerDebuffs(uint16_t specId, uint32_t spell_id) const;
    void setAuraExcludedFromPlayerBuffs(uint16_t specId, uint32_t spell_id, bool excluded);
    void setAuraExcludedFromPlayerDebuffs(uint16_t specId, uint32_t spell_id, bool excluded);

    // Version counter - incremented whenever spell assignments change
    // Used by other components to detect when to refresh their caches
    uint32_t getVersion() const { return version_; }

    // Singleton instance
    static SpellAssignmentSettings& instance();

private:
    std::unordered_map<uint16_t, SpecSpellAssignments> specAssignments_;  // Keyed by spec ID
    bool usingDefaults_ = true;
    uint32_t version_ = 0;  // Incremented on any change for cache invalidation

    // GCD exclusion cache: spec -> set of excluded spell IDs
    mutable std::unordered_map<uint16_t, std::unordered_set<uint32_t>> gcdExclusionCache_;
    mutable bool gcdCacheDirty_ = true;

    void rebuildGCDExclusionCache() const;

    // Nameplate inclusion cache: flat set of spell IDs enabled for Nameplate
    // Aggregated across all specs for fast O(1) lookup
    mutable std::unordered_set<uint32_t> nameplateAuraCache_;
    mutable bool nameplateCacheDirty_ = true;

    void rebuildNameplateCache() const;

    // Build default spell and aura assignments (empty for now - user configures per spec)
    void applyDefaults();
};

} // namespace spell_assignment
