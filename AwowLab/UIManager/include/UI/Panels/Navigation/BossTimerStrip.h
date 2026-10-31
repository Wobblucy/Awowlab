#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_set>

class SpellIconLoader;
class PlaybackController;
class SpellCastDatabase;
class AuraDatabase;
class CombatDatabase;
class ActorColorGenerator;
struct BossTimerSettings;
struct HostileCreatureSpells;

// Event type for boss timer
enum class BossTimerEventType : uint8_t {
    Cast,       // SPELL_CAST_SUCCESS
    Damage,     // SPELL_DAMAGE
    Debuff      // SPELL_AURA_APPLIED (debuff)
};

// Single boss timer event
struct BossTimerEvent {
    int32_t timestamp_ms = 0;
    uint32_t spell_id = 0;
    std::string source_name;
    BossTimerEventType type = BossTimerEventType::Cast;
};

// Grouped events for rendering (events within grouping window)
struct BossTimerEventGroup {
    int32_t timestamp_ms = 0;     // Earliest timestamp in group
    uint32_t spell_id = 0;        // Representative spell ID
    std::string source_name;
    BossTimerEventType type = BossTimerEventType::Cast;
    uint32_t count = 1;           // Number of events in this group
    float normalizedPos = 0.0f;   // Position on timeline [0.0, 1.0]
    int row = 0;                  // Row for auto-expand (0-based)
    std::vector<int32_t> groupedTimestamps;  // All timestamps in this group (for raid notes)
};

// Timeline strip showing hostile creature actions
// Renders below the playback bar
class BossTimerStrip {
public:
    BossTimerStrip();
    ~BossTimerStrip();

    // Render the timer strip
    void render();

    // Get height of the strip in pixels (for layout)
    float getHeight() const;

    // Set dependencies
    void setSpellIconLoader(SpellIconLoader* loader) { iconLoader_ = loader; }
    void setPlaybackController(PlaybackController* controller) { playback_ = controller; }
    void setSettings(BossTimerSettings* settings) { settings_ = settings; }

    // Load hostile creature spells (from MapVisualizer::buildHostileCreatureSpells)
    // This defines which spells are from hostile sources
    void setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& creatureSpells);

    // Set database references for event timing (called when encounter loads)
    void setDatabases(
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        const CombatDatabase* combatDb,
        const ActorColorGenerator* colorGenerator
    );

    // Mark cache as needing rebuild
    void markDirty() { cacheDirty_ = true; }
    bool isDirty() const { return cacheDirty_; }

private:
    SpellIconLoader* iconLoader_ = nullptr;
    PlaybackController* playback_ = nullptr;
    BossTimerSettings* settings_ = nullptr;

    // Database references for event timing
    const SpellCastDatabase* spellCastDb_ = nullptr;
    const AuraDatabase* auraDb_ = nullptr;
    const CombatDatabase* combatDb_ = nullptr;
    const ActorColorGenerator* colorGenerator_ = nullptr;

    // Set of hostile spell IDs (for fast lookup)
    std::unordered_set<uint32_t> hostileCastIds_;
    std::unordered_set<uint32_t> hostileDebuffIds_;
    std::unordered_set<uint32_t> hostileDamageIds_;

    // Event data
    std::vector<BossTimerEvent> rawEvents_;
    std::vector<BossTimerEventGroup> eventGroups_;
    bool cacheDirty_ = true;

    // Time bounds for normalization
    int32_t minTime_ = 0;
    int32_t maxTime_ = 0;

    // Number of rows (for auto-expand)
    int rowCount_ = 1;

    // Rebuild the event cache from databases
    void rebuildEventCache();

    // Collect hostile events from databases
    void collectHostileEvents();

    // Group events by spell_id + type within time window and filter by settings
    void groupAndFilterEvents();

    // Calculate row assignments for auto-expand
    void calculateRowAssignments();

    // Render a single event icon
    void renderEventIcon(const BossTimerEventGroup& group, float x, float y, float iconSize);

    // Render tooltip for event group
    void renderTooltip(const BossTimerEventGroup& group);

    // Get color for event type
    uint32_t getEventTypeColor(BossTimerEventType type) const;
};
