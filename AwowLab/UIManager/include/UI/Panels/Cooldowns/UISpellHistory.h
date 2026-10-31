#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <optional>

// Forward declaration
class SpellIconLoader;

// Spell cast record for UI display
struct SpellCastDisplay {
    uint32_t spell_id;
    int32_t start_timestamp_ms;       // When cast began (0 or same as end = instant, negative = before encounter)
    int32_t end_timestamp_ms;         // When cast completed (or cancelled)
    int32_t gcd_ms;                   // Expected GCD duration in ms (0 = off-GCD ability)
    int32_t expected_cast_time_ms;    // Expected cast time in ms (0 = instant)
    uint16_t haste_permille;          // Haste at time of cast (300 = 30.0%)
    bool cancelled = false;           // True if cast was started but never completed

    bool is_instant() const {
        return expected_cast_time_ms == 0;
    }
    bool has_cast_time() const {
        return expected_cast_time_ms > 0;
    }
    int32_t actual_cast_duration_ms() const {
        if (start_timestamp_ms == 0 || start_timestamp_ms >= end_timestamp_ms) return 0;
        return end_timestamp_ms - start_timestamp_ms;
    }
    float haste_percent() const {
        return static_cast<float>(haste_permille) / 10.0f;
    }
};

class UISpellHistory {
public:
    UISpellHistory() = default;

    // Render the spell history panel
    // Returns true if window is open
    bool render(const std::optional<std::string>& selectedActorGuid,
                const std::vector<SpellCastDisplay>& recentCasts,
                int32_t currentTime,
                SpellIconLoader* iconLoader,
                bool asWindow = true);

private:
    void renderSpellBar(const std::vector<SpellCastDisplay>& recentCasts,
                       int32_t currentTime,
                       SpellIconLoader* iconLoader,
                       const std::string& actorGuid);

    // Animation state
    float scroll_offset_ = 0.0f;
    double last_update_time_ = 0.0;
};
