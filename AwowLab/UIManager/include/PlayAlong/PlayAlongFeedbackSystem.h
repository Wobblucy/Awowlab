#pragma once

#include "PlayAlongKeybindSettings.h"
#include <vector>
#include <deque>
#include <string>
#include <cstdint>

class SpellCastDatabase;

namespace PlayAlong {

// A player keybind press
struct PlayerInput {
    Keybind keybind;            // The actual key+modifiers pressed
    int32_t timestampMs;        // When pressed (playback time)
    bool processed = false;     // Has this been evaluated?
};

// Result of evaluating a player input
enum class FeedbackResult {
    Good,       // Correct spell in valid window
    Bad,        // Wrong spell or outside window
    Neutral,    // Early press in queue window for next ability (not penalized)
    Pending     // Not yet evaluated (waiting for time to pass)
};

struct FeedbackEvent {
    uint32_t spellId;
    int32_t playerPressMs;      // When player pressed
    int32_t expectedCastMs;     // When log expected it (0 if no match)
    int32_t deltaMs;            // playerPress - expectedCast (negative = early)
    FeedbackResult result;
};

// Tracks expected casts from log
struct ExpectedCast {
    uint32_t spellId;
    int32_t timestampMs;        // From log
    bool matched = false;       // Has a player input matched this?
    int32_t matchedAtMs = 0;    // When it was matched
};

// 15-second section score
struct SectionScore {
    int32_t startMs;
    int32_t endMs;
    uint32_t goodCount = 0;
    uint32_t badCount = 0;
    uint32_t missedCount = 0;   // Expected casts with no matching input

    float accuracy() const {
        uint32_t total = goodCount + badCount + missedCount;
        if (total == 0) return 1.0f;
        return static_cast<float>(goodCount) / static_cast<float>(total);
    }
};

class PlayAlongFeedbackSystem {
public:
    // Configuration
    void setSpellQueueWindowMs(int32_t ms) { spellQueueWindowMs_ = ms; }
    int32_t getSpellQueueWindowMs() const { return spellQueueWindowMs_; }
    static constexpr int32_t LATE_WINDOW_MS = 100;

    // Initialize with keybinds and spell cast database
    void initialize(const PlayAlongKeybindSettings& keybinds,
                    const SpellCastDatabase* spellCastDb,
                    const std::string& targetPlayerGuid);

    // Reset all state (on encounter restart)
    void reset();

    // Reset to a specific time (on rewind) - clears scores and repositions
    void resetToTime(int32_t timeMs);

    // Record a player keybind press
    void recordPlayerInput(const Keybind& keybind, int32_t timestampMs);

    // Process inputs and generate feedback events
    // Call each frame with current playback time
    void update(int32_t currentTimeMs);

    // Get recent feedback events (for UI display)
    const std::deque<FeedbackEvent>& getRecentFeedback() const { return recentFeedback_; }

    // Get section scores for report card
    std::vector<SectionScore> getSectionScores() const;

    // Overall score
    float getOverallAccuracy() const;

    uint32_t getTotalGood() const { return totalGood_; }
    uint32_t getTotalBad() const { return totalBad_; }
    uint32_t getTotalMissed() const { return totalMissed_; }

    // Get keybind settings (for checking ignored spells)
    const PlayAlongKeybindSettings& getKeybinds() const { return keybinds_; }

    bool isInitialized() const { return spellCastDb_ != nullptr; }

private:
    int32_t spellQueueWindowMs_ = 300;  // Configurable "spell queue window"

    PlayAlongKeybindSettings keybinds_;
    const SpellCastDatabase* spellCastDb_ = nullptr;
    std::string targetPlayerGuid_;

    // Expected casts from log (sorted by timestamp)
    std::vector<ExpectedCast> expectedCasts_;
    size_t nextExpectedIndex_ = 0;  // Index of next unmatched expected cast

    // Player inputs awaiting evaluation
    std::deque<PlayerInput> pendingInputs_;

    // Recent feedback events (for UI, capped at 50)
    std::deque<FeedbackEvent> recentFeedback_;
    static constexpr size_t MAX_RECENT_FEEDBACK = 50;

    // All feedback events (for report card)
    std::vector<FeedbackEvent> allFeedback_;

    // Scoring
    uint32_t totalGood_ = 0;
    uint32_t totalBad_ = 0;
    uint32_t totalMissed_ = 0;

    // Track encounter time range
    int32_t minTimeMs_ = 0;
    int32_t maxTimeMs_ = 0;

    void loadExpectedCasts();
    void processInput(PlayerInput& input, int32_t currentTimeMs);
    void checkForMissedCasts(int32_t currentTimeMs);
};

} // namespace PlayAlong
