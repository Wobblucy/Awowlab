#pragma once
#include "PullSegment.h"
#include "LiveLogSession.h"
#include "Database/CombatDatabase.h"
#include "Database/DispelDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/ResourceDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/AbsorbDatabase.h"
#include "Database/AvoidanceDatabase.h"
#include <memory>
#include <vector>
#include <chrono>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

// View modes for stats aggregation
enum class StatsViewMode {
    CurrentPull,      // Stats for current/latest pull only
    HistoricalPull,   // Stats for a selected historical pull
    CurrentDungeon,   // Cumulative stats since CHALLENGE_MODE_START
    TrashOverall,     // Aggregate all trash pulls in current dungeon
    SessionTotal      // All stats since overlay started
};

// Time-windowed meter queries over a LiveLogSession, for each view mode.
//
//   LiveCombatStats stats;
//   stats.attachSession(&session);
//   auto damage = stats.getDamageRanked(StatsViewMode::CurrentPull, 20);
//
class LiveCombatStats {
public:
    LiveCombatStats();
    ~LiveCombatStats();

    // Attach to a live log session (non-owning pointer)
    void attachSession(LiveLogSession* session);

    // Detach from session
    void detachSession();

    // Rebuild internal databases (call after session data updates)
    void refresh();

    // Get damage rankings (DPS)
    std::vector<ActorCombatStats> getDamageRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get healing rankings (HPS)
    std::vector<ActorCombatStats> getHealingRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get damage taken rankings (DTPS)
    std::vector<ActorCombatStats> getDamageTakenRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get interrupt rankings
    std::vector<ActorDispelStats> getInterruptsRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get dispel rankings
    std::vector<ActorDispelStats> getDispelsRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Re-parses the pull from its byte offsets on a worker thread
    void selectHistoricalPull(size_t pullIndex);

    // Re-parses every segment of the current dungeon into one view
    void selectOverallView();

    // Clear historical pull selection (return to CurrentPull/live mode)
    void clearHistoricalPullSelection();

    // Get the currently selected historical pull index (SIZE_MAX if none)
    size_t getSelectedHistoricalPullIndex() const { return selectedPullIndex_; }

    // Check if a historical pull is selected
    bool hasHistoricalPullSelected() const { return selectedPullIndex_ != SIZE_MAX; }

    // Get the time range for a given view mode
    // Returns (start_ms, end_ms) relative to log start
    std::pair<int32_t, int32_t> getTimeRange(StatsViewMode mode) const;

    // Get duration in seconds for a given view mode
    float getDurationSeconds(StatsViewMode mode) const;

    // Get encounter/dungeon name for display
    std::string getCurrentSegmentName() const;

    // Check if currently in an encounter
    bool isInEncounter() const;

    // Check if currently in M+ dungeon
    bool isInMythicPlus() const;

    // The databases behind UIMeterPanel, rebuilt by refresh()

    // Get the internal CombatDatabase (for UIMeterPanel)
    const CombatDatabase* getCombatDatabase() const { return combatDb_.get(); }

    // Get the internal DispelDatabase (for UIMeterPanel)
    const DispelDatabase* getDispelDatabase() const { return dispelDb_.get(); }

    // Get the internal DeathDatabase (for UIMeterPanel death view)
    const DeathDatabase* getDeathDatabase() const { return deathDb_.get(); }

    // Get the internal ResourceDatabase (for UIMeterPanel death recap)
    const ResourceDatabase* getResourceDatabase() const { return resourceDb_.get(); }

    // Get the internal AuraDatabase (for UIMeterPanel CC breaks view)
    const AuraDatabase* getAuraDatabase() const { return auraDb_.get(); }

    // Get the internal AbsorbDatabase (for UIMeterPanel Absorbs view)
    const AbsorbDatabase* getAbsorbDatabase() const { return absorbDb_.get(); }

    // Get the internal AvoidanceDatabase (for UIMeterPanel Avoidance view)
    const AvoidanceDatabase* getAvoidanceDatabase() const { return avoidanceDb_.get(); }

private:
    LiveLogSession* session_ = nullptr;

    // Internal databases (rebuilt on refresh)
    std::unique_ptr<CombatDatabase> combatDb_;
    std::unique_ptr<DispelDatabase> dispelDb_;
    std::unique_ptr<DeathDatabase> deathDb_;
    std::unique_ptr<ResourceDatabase> resourceDb_;
    std::unique_ptr<AuraDatabase> auraDb_;
    std::unique_ptr<AbsorbDatabase> absorbDb_;
    std::unique_ptr<AvoidanceDatabase> avoidanceDb_;

    // An index, since a pointer would dangle when the history grows
    size_t selectedPullIndex_ = SIZE_MAX;

    // Session-level timestamps
    int32_t dungeonStartTime_ms_ = 0;  // Set on CHALLENGE_MODE_START
    int32_t sessionStartTime_ms_ = 0;  // Set when session starts

    // Segment selection parses on a detached worker, since a boss segment
    // can take seconds. selectMutex_ keeps two parses from overlapping.
    std::mutex selectMutex_;
    std::atomic<bool> selectInFlight_{false};

    // Runs work() on a detached worker under selectMutex_
    void launchAsyncSelect(std::function<void()> work);

    // Helper to get time range for current/historical pull
    std::pair<int32_t, int32_t> getPullTimeRange(const PullSegment* pull) const;

    // Helper to get all trash pull time ranges in current dungeon
    std::vector<std::pair<int32_t, int32_t>> getTrashPullTimeRanges() const;
};
