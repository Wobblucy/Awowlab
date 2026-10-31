#pragma once
#include <map>
#include <vector>
#include <algorithm>
#include "OutputWriter.h"
#include "Core/StringInterner.h"

// EventDatabase uses a std::multimap (red-black tree) for efficient
// timestamp-based storage and range queries.
// Stores ActorEventCompact internally for memory efficiency (~28 bytes vs ~100 bytes per event)
// Expands to full ActorEvent during iteration.
class EventDatabase {
private:
    std::multimap<int32_t, ActorEventCompact> events_;  // Key: timestamp_ms (negative = before encounter)
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;

    // Convert full ActorEvent to compact form for storage
    static ActorEventCompact toCompact(const ActorEvent& event) {
        ActorEventCompact compact{};
        compact.guid_id = guidInterner().intern(event.source_guid);
        compact.name_id = actorNameInterner().intern(event.source_actor);
        compact.timestamp_ms = event.timestamp_ms;
        compact.x_loc = event.x_loc;
        compact.y_loc = event.y_loc;
        compact.map_id = event.map_id;
        compact.actor_type = static_cast<uint8_t>(event.actor_type);
        compact.flags_affiliation = static_cast<uint8_t>(event.flags.affiliation) & 0x3;
        compact.flags_reaction = static_cast<uint8_t>(event.flags.reaction) & 0x3;
        compact.flags_controller = static_cast<uint8_t>(event.flags.controller) & 0x1;
        compact.flags_unit_type = static_cast<uint8_t>(event.flags.unitType) & 0x7;
        compact.padding = 0;
        compact.facing_radians = event.facing_radians;
        return compact;
    }

    // Expand compact event to full ActorEvent for callbacks (zero-copy)
    static ActorEvent fromCompact(const ActorEventCompact& compact) {
        ActorEvent event{};
        event.source_guid = guidInterner().lookup(compact.guid_id);       // string_view, no allocation
        event.source_actor = actorNameInterner().lookup(compact.name_id); // string_view, no allocation
        event.timestamp_ms = compact.timestamp_ms;
        event.x_loc = compact.x_loc;
        event.y_loc = compact.y_loc;
        event.map_id = compact.map_id;
        event.actor_type = static_cast<ActorType>(compact.actor_type);
        event.flags.affiliation = static_cast<UnitAffiliation>(compact.flags_affiliation);
        event.flags.reaction = static_cast<UnitReaction>(compact.flags_reaction);
        event.flags.controller = static_cast<UnitController>(compact.flags_controller);
        event.flags.unitType = static_cast<UnitType>(compact.flags_unit_type);
        event.facing_radians = compact.facing_radians;
        return event;
    }

public:
    // Load events from a vector (one-time initialization)
    void loadEvents(const std::vector<ActorEvent>& events) {
        events_.clear();

        if (events.empty()) {
            min_timestamp_ = 0;
            max_timestamp_ = 0;
            return;
        }

        // Insert all events as compact form into the multimap
        for (const auto& event : events) {
            events_.insert({event.timestamp_ms, toCompact(event)});
        }

        min_timestamp_ = events_.begin()->first;
        max_timestamp_ = events_.rbegin()->first;
    }

    // Iteration over events in a time window (expands to full ActorEvent)
    // Callback signature: void(const ActorEvent&)
    template<typename Func>
    void forEachEventInWindow(int32_t center_time_ms, int32_t window_ms, Func&& callback) const {
        if (events_.empty()) {
            return;
        }

        // Calculate time range (signed arithmetic handles negative timestamps naturally)
        int32_t start_time = center_time_ms - window_ms;
        int32_t end_time = center_time_ms + window_ms;

        // Find range using lower_bound and upper_bound (O(log n))
        auto start_it = events_.lower_bound(start_time);
        auto end_it = events_.upper_bound(end_time);

        // Expand compact events to full ActorEvent for callback
        for (auto it = start_it; it != end_it; ++it) {
            callback(fromCompact(it->second));
        }
    }

    // Iteration over all events (expands to full ActorEvent)
    // Callback signature: void(const ActorEvent&)
    template<typename Func>
    void forEachEvent(Func&& callback) const {
        for (const auto& pair : events_) {
            callback(fromCompact(pair.second));
        }
    }

    // Get timestamp range (can be negative for pre-encounter events)
    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }
    size_t size() const { return events_.size(); }
    bool empty() const { return events_.empty(); }

    // Get mapId from the first event with valid map_id (useful when MAP_CHANGE events don't fire)
    uint16_t getFirstMapId() const {
        for (const auto& [key, event] : events_) {
            if (event.map_id != 0) {
                return event.map_id;
            }
        }
        return 0;
    }

    // Get position from the first event (useful for fallback grid centering)
    float getFirstX() const {
        if (events_.empty()) return 0.0f;
        return events_.begin()->second.x_loc;
    }

    float getFirstY() const {
        if (events_.empty()) return 0.0f;
        return events_.begin()->second.y_loc;
    }
};
