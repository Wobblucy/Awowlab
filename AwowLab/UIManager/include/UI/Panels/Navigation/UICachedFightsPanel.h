#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <utility>
#include <unordered_map>

// Forward declarations
class ClassIconCache;

namespace warcraftlogs {
    class ResponseCache;
}

/**
 * Data structure representing a cached fight for display.
 */
struct CachedFightInfo {
    std::string reportCode;      // Report code for loading
    int32_t fightId = 0;         // Fight ID within report
    std::string fightName;       // Display name (boss/dungeon name)
    int32_t encounterID = 0;     // WoW encounter ID (0 for non-boss)
    int32_t difficulty = 0;      // Difficulty level
    int32_t keystoneLevel = 0;   // M+ keystone level (0 if not M+)
    int64_t durationMs = 0;      // Fight duration in milliseconds
    int64_t startTimeMs = 0;     // Absolute start time (Unix epoch ms), 0 = unknown
    bool fullyLoaded = false;    // Whether event data is cached (not just metadata)
    std::string firstPlayerName; // Name of first player in the group
    std::vector<std::pair<std::string, int>> classComposition;  // Class name -> count pairs

    // Computed properties
    [[nodiscard]] bool isMythicPlus() const noexcept { return keystoneLevel > 0 || difficulty == 10; }
    [[nodiscard]] bool isBossEncounter() const noexcept { return encounterID > 0 && !isMythicPlus(); }
    [[nodiscard]] std::string difficultyName() const;
};

/**
 * Sort mode for cached fights list.
 */
enum class CachedFightSortMode {
    DateNewest,   // Most recent first (default)
    DateOldest,   // Oldest first
    NameAZ,       // Alphabetical A-Z
    NameZA        // Alphabetical Z-A
};

/**
 * Groups cached fights by type for organized display.
 */
enum class FightGroupType {
    MythicPlus,      // M+ dungeons (keystone level set, or difficulty == 10)
    RaidEncounter,   // Raid bosses (encounterID > 0, not M+)
    Other            // Trash, wipes, etc.
};

/**
 * Subgroup of fights sharing the same encounter/dungeon name.
 */
struct EncounterSubgroup {
    std::string encounterName;    // e.g., "Queen Ansurek" or "Ara-Kara, City of Echoes"
    std::vector<CachedFightInfo> fights;
};

/**
 * A group of fights with a header label, organized by encounter subgroups.
 */
struct CachedFightGroup {
    FightGroupType type;
    std::string label;
    std::vector<EncounterSubgroup> subgroups;
};

/**
 * Result from the cached fights panel.
 */
struct CachedFightSelection {
    bool selected = false;       // True if user selected a fight
    bool cancelled = false;      // True if user cancelled
    std::string reportCode;      // Selected report code
    int32_t fightId = -1;        // Selected fight ID
};

/**
 * Panel for browsing and selecting cached WarcraftLogs fights.
 *
 * Displays all cached reports and their fights, grouped by type:
 * - Mythic+ dungeons
 * - Raid encounters
 * - Other (trash, etc.)
 *
 * Within a group, encounters are listed by name and their fights follow
 * the chosen sort mode.
 */
class UICachedFightsPanel {
public:
    UICachedFightsPanel();
    ~UICachedFightsPanel() = default;

    /**
     * Set the response cache to read cached data from.
     */
    void setCache(warcraftlogs::ResponseCache* cache);

    /**
     * Refresh the cached fights list from disk.
     * Call this before rendering if cache may have changed.
     */
    void refreshCacheList();

    /**
     * Render the panel as a modal popup.
     * @param iconCache Optional ClassIconCache for rendering class icons
     * @return True if selection/cancellation occurred.
     */
    bool render(ClassIconCache* iconCache = nullptr);

    /**
     * Get the selection result.
     */
    [[nodiscard]] const CachedFightSelection& getSelection() const { return selection_; }

    /**
     * Check if there are any cached fights.
     */
    [[nodiscard]] bool hasCachedFights() const { return !groups_.empty(); }

    /**
     * Get total count of cached fights.
     */
    [[nodiscard]] size_t getCachedFightCount() const;

    /**
     * Every cached fight, flattened out of the display grouping. The benchmark
     * pickers need to filter by encounter rather than show the grouped list,
     * and reusing what is already scanned avoids a second walk over the cache.
     */
    [[nodiscard]] std::vector<CachedFightInfo> getAllFights() const;

    /**
     * Encounter id -> display name, for every cached fight that has one.
     * Lets a picker name an encounter without reloading anything.
     */
    [[nodiscard]] std::unordered_map<uint32_t, std::string> getEncounterNames() const;

    /**
     * Open the panel.
     */
    void open();

    /**
     * Reset panel state.
     */
    void reset();

private:
    /**
     * Build grouped fight list from cache.
     */
    void buildGroupedList();

    /**
     * Sort fights within a group based on current sort mode.
     */
    void sortFightsInGroup(std::vector<CachedFightInfo>& fights) const;

    /**
     * Render a single fight group (category level).
     */
    void renderGroup(const CachedFightGroup& group, int groupIndex, ClassIconCache* iconCache);

    /**
     * Render a single encounter subgroup within a fight group.
     */
    void renderSubgroup(const EncounterSubgroup& subgroup, int groupIndex, int subgroupIndex, ClassIconCache* iconCache);

    warcraftlogs::ResponseCache* cache_ = nullptr;
    std::vector<CachedFightGroup> groups_;
    CachedFightSelection selection_;
    bool isOpen_ = false;
    bool popupOpened_ = false;  // Track if OpenPopup was called this session

    // Selection state (3-level: group -> subgroup -> fight)
    int selectedGroupIndex_ = -1;
    int selectedSubgroupIndex_ = -1;
    int selectedFightIndex_ = -1;

    // Filter options
    bool showMetadataOnly_ = false;  // Show fights with only metadata (no event data)

    // Sort and filter state
    CachedFightSortMode sortMode_ = CachedFightSortMode::DateNewest;
    int difficultyFilter_ = -1;  // -1 = All, otherwise specific difficulty value

    // Delete confirmation state
    bool showDeleteConfirm_ = false;
    std::string deleteTargetDescription_;
    std::string deletePendingReportCode_;
    int32_t deletePendingFightId_ = -1;

    /**
     * Request deletion of a specific fight (shows confirmation dialog).
     */
    void requestDeleteFight(const CachedFightInfo& fight);

    // Report the fight back to the caller and close the popup
    void chooseFight(const CachedFightInfo& fight);

    void clearSelection();       // result, highlighted row and pending delete
    void clearHighlightedRow();
    void clearPendingDelete();
};
