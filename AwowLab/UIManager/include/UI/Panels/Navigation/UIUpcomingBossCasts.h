#pragma once

#include <vector>
#include <string>
#include <cstdint>

class SpellIconLoader;
class SpellCastDatabase;
class ActorColorGenerator;
struct BossTimerSettings;

// Single upcoming hostile cast entry
struct UpcomingCast {
    uint32_t spell_id = 0;
    int32_t timestamp_ms = 0;       // Actual cast time from log
    std::string source_name;        // Boss/creature name
};

// Vertical sidebar panel showing upcoming boss abilities with countdown timers
// Uses actual cast times from the loaded combat log data
class UIUpcomingBossCasts {
public:
    UIUpcomingBossCasts();
    ~UIUpcomingBossCasts();

    // Render the panel (right-side sidebar)
    void render(int32_t currentTimeMs);

    // Set dependencies (call when encounter loads)
    void setDependencies(
        SpellIconLoader* iconLoader,
        const SpellCastDatabase* spellCastDb,
        BossTimerSettings* settings,
        const ActorColorGenerator* colorGenerator
    );

    // Mark cache as needing rebuild (call when filters change)
    void markDirty() { cacheDirty_ = true; }

    // Get panel width for layout calculations
    float getWidth() const;

    // Check if panel is enabled
    bool isEnabled() const;

private:
    SpellIconLoader* iconLoader_ = nullptr;
    const SpellCastDatabase* spellCastDb_ = nullptr;
    BossTimerSettings* settings_ = nullptr;
    const ActorColorGenerator* colorGenerator_ = nullptr;

    // Pre-filtered hostile casts (rebuilt when encounter loads or filters change)
    std::vector<UpcomingCast> allHostileCasts_;
    bool cacheDirty_ = true;

    // Rebuild the cache of hostile casts
    void rebuildCache();

    // Find first cast index >= currentTime using binary search
    size_t findFirstUpcomingCast(int32_t currentTimeMs) const;
};
