#pragma once

#include "PlayAlongFeedbackSystem.h"
#include <cstdint>

namespace PlayAlong {

// Always-on stats panel: accuracy, streaks and counts
class UIPlayAlongStatsPanel {
public:
    UIPlayAlongStatsPanel() = default;

    void render(const PlayAlongFeedbackSystem* feedbackSystem);

private:
    // Streaks are tracked here; the feedback system only keeps totals
    uint32_t currentStreak_ = 0;
    uint32_t bestStreak_ = 0;
    uint32_t lastGoodCount_ = 0;
    uint32_t lastBadCount_ = 0;

    static constexpr size_t RECENT_WINDOW = 15;
};

} // namespace PlayAlong
