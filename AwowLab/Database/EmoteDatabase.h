#pragma once

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

#include "EventTypes.h"

// Holds the emote lines of the loaded encounter, sorted by time.
// Phase rules resolve their boundaries against this ("phase 2 starts
// at the first emote saying X"), and the phase editor lists the
// pull's emotes so users can pick a split point by clicking one.
class EmoteDatabase {
public:
    void loadFromEvents(std::vector<EmoteEvent> events) {
        emotes_ = std::move(events);
        std::sort(emotes_.begin(), emotes_.end(),
                  [](const EmoteEvent& a, const EmoteEvent& b) {
                      return a.timestamp_ms < b.timestamp_ms;
                  });
    }

    const std::vector<EmoteEvent>& getEmotes() const { return emotes_; }

    bool empty() const { return emotes_.empty(); }

    // Time of the first emote whose cleaned text matches exactly, or
    // nullopt when it never fired this pull
    std::optional<int32_t> firstEmoteTime(std::string_view text) const {
        for (const auto& emote : emotes_) {
            if (emote.text == text) {
                return emote.timestamp_ms;
            }
        }
        return std::nullopt;
    }

private:
    std::vector<EmoteEvent> emotes_;
};
