#pragma once

#include <string>
#include <unordered_set>
#include <cstdint>

// Settings for the Boss Timer feature
// Persisted via UnifiedSettings as JSON
struct BossTimerSettings {
    // Master visibility toggle (beside Follow button in playback bar)
    bool enabled = true;

    // Event type toggles (casts shown by default, others hidden)
    bool showCasts = true;
    bool showDamage = false;
    bool showDebuffs = false;

    // Enabled spell IDs (whitelist - only these spells are shown)
    // Spells must be explicitly enabled to appear on the timer
    std::unordered_set<uint32_t> enabledSpellIds;

    // Display settings for horizontal timeline strip
    float iconSize = 24.0f;            // Adjustable 16-48px
    uint32_t groupingWindowMs = 5000;  // Events within this window are grouped (default 5s)

    // Upcoming Boss Casts panel settings (vertical sidebar)
    bool upcomingCastsPanelEnabled = true;    // Master toggle for the sidebar
    float upcomingWindowSeconds = 30.0f;      // How far ahead to show (seconds)
    float upcomingIconSize = 32.0f;           // Icon size in sidebar
    float upcomingPanelWidth = 200.0f;        // Sidebar width

    // Fast O(1) lookup for enabled spells (whitelist)
    bool isSpellEnabled(uint32_t spellId) const {
        return enabledSpellIds.find(spellId) != enabledSpellIds.end();
    }

    void enableSpell(uint32_t spellId) {
        enabledSpellIds.insert(spellId);
    }

    void disableSpell(uint32_t spellId) {
        enabledSpellIds.erase(spellId);
    }

    void toggleSpell(uint32_t spellId) {
        if (isSpellEnabled(spellId)) {
            disableSpell(spellId);
        } else {
            enableSpell(spellId);
        }
    }

    // JSON serialization for UnifiedSettings
    std::string serializeToJson() const;
    static BossTimerSettings deserializeFromJson(const std::string& json);
};
