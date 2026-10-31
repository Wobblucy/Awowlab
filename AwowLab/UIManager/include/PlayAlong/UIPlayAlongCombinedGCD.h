#pragma once

#include "PlayAlongFeedbackSystem.h"
#include <vector>
#include <string>
#include <cstdint>

// Forward declarations
class SpellIconLoader;
class SpellCastDatabase;

namespace PlayAlong {

// Spell cast info for display
struct SpellCastDisplay {
    uint32_t spell_id;
    int32_t start_timestamp_ms;
    int32_t end_timestamp_ms;
    uint16_t gcd_duration_ms;
    uint16_t cast_duration_ms;
    int16_t haste_permille;
    bool cancelled;
};

// Movable panel with the player's inputs on one row and the logged casts on the other
class UIPlayAlongCombinedGCD {
public:
    UIPlayAlongCombinedGCD() = default;

    // feedbackSystem may be null, in which case the input row stays empty
    void render(const std::string& targetPlayerGuid,
                int32_t currentTimeMs,
                const SpellCastDatabase* spellCastDb,
                SpellIconLoader* iconLoader,
                const PlayAlongFeedbackSystem* feedbackSystem);

private:
    static constexpr float PAST_WINDOW_SECONDS = 3.0f;
    static constexpr float FUTURE_WINDOW_SECONDS = 5.0f;
    static constexpr float ROW_HEIGHT = 52.0f;
    static constexpr float ICON_SIZE = 40.0f;
    static constexpr float PANEL_WIDTH = 600.0f;
};

} // namespace PlayAlong
