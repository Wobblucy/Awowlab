#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <rapidjson/document.h>

namespace warcraftlogs {

struct Event {
    int64_t timestamp = 0;       // Event time in milliseconds
    std::string type;            // Event type (e.g., "cast", "damage", "heal")
    int32_t fight = 0;           // Fight ID

    // Source/target actors (may be -1 if not applicable)
    int32_t sourceID = -1;
    int32_t targetID = -1;

    // Optional ability info
    std::optional<int32_t> abilityGameID;
    std::optional<std::string> abilityName;

    // Resource info (for resource change events)
    std::optional<int32_t> resourceChange;
    std::optional<int32_t> resourceChangeType;

    // Damage/heal specific
    std::optional<int64_t> amount;
    std::optional<int64_t> absorbed;
    std::optional<bool> isCritical;

    // Position data (if available)
    std::optional<float> x;
    std::optional<float> y;
    std::optional<int32_t> mapID;
    std::optional<float> facing;

    // The whole event, for fields not parsed above.
    rapidjson::Document rawData;

    // rapidjson::Document can't be copied, so Event is move-only.
    Event() = default;
    ~Event() = default;
    Event(Event&&) noexcept = default;
    Event& operator=(Event&&) noexcept = default;
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    [[nodiscard]] bool hasPosition() const noexcept {
        return x.has_value() && y.has_value();
    }

    [[nodiscard]] bool hasSource() const noexcept {
        return sourceID >= 0;
    }

    [[nodiscard]] bool hasTarget() const noexcept {
        return targetID >= 0;
    }

    [[nodiscard]] bool isDamageEvent() const noexcept {
        return type == "damage" || type == "spell_damage" || type == "swing_damage";
    }

    [[nodiscard]] bool isHealEvent() const noexcept {
        return type == "heal" || type == "healing";
    }

    [[nodiscard]] bool isCastEvent() const noexcept {
        return type == "cast" || type == "begincast";
    }

    [[nodiscard]] bool isDeathEvent() const noexcept {
        return type == "death";
    }
};

} // namespace warcraftlogs
