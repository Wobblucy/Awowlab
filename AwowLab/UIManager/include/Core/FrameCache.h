#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <cstdint>
#include "../../Database/OutputWriter.h"
#include "Animation/ActorPositionInterpolator.h"
#include "Core/FrameProfiler.h"

// Caches computed data for a single frame. Every UI panel needs the same event
// window and interpolated positions, so they are computed once and shared.
// Colors are not cached here; use ActorColorGenerator for those.
class FrameCache {
public:
    FrameCache() = default;

    // Tolerates a small time drift so playback does not re-collect every frame
    // (50ms is about 3 frames at 60fps, which keeps interpolation smooth)
    bool isValid(int32_t currentTime, uint32_t timeWindow) const {
        if (!valid_ || cachedWindow_ != timeWindow) {
            return false;
        }
        // Timestamps are signed: pre-encounter events are negative
        int32_t timeDiff = currentTime - cachedTime_;
        if (timeDiff < 0) timeDiff = -timeDiff;
        return static_cast<uint32_t>(timeDiff) < TIME_TOLERANCE_MS;
    }

    static constexpr uint32_t TIME_TOLERANCE_MS = 50;

    void invalidate() {
        valid_ = false;
    }

    // Call when loading a new database
    void clear() {
        valid_ = false;
        positionsValid_ = false;
        resourcesValid_ = false;
        cachedTime_ = 0;
        cachedWindow_ = 0;
        guidMapsPopulated_ = false;
        windowEvents_.clear();
        guidToName_.clear();
        guidToTruncatedName_.clear();
        guidToType_.clear();
        actorPositions_.clear();
        actorResources_.clear();
    }

    // Called during the initial database load so every actor is known before rendering starts
    void cacheActorInfo(const ActorEvent& event) {
        if (guidToName_.find(event.source_guid) == guidToName_.end()) {
            auto sv = event.source_guid;
            guidToName_.emplace(sv, event.source_actor);
            guidToType_.emplace(sv, event.actor_type);
            guidToTruncatedName_.emplace(sv, event.source_actor.substr(
                0, std::min<size_t>(3, event.source_actor.length())));
        }
    }

    // Once populated, the per-frame GUID map updates are skipped
    void markGuidMapsPopulated() {
        guidMapsPopulated_ = true;
    }

    void setWindowEvents(int32_t currentTime, uint32_t timeWindow,
                         const std::vector<ActorEvent>& events) {
        cachedTime_ = currentTime;
        cachedWindow_ = timeWindow;

        {
            FRAME_PROFILE_SCOPE("WindowEvents::CopyVec");
            windowEvents_ = events;
        }

        updateGuidMapsIfNeeded();
        valid_ = true;
        positionsValid_ = false;
        resourcesValid_ = false;
    }

    // Swaps rather than move-assigns so the caller's reusable buffer gets our
    // previous vector back - both allocations stay warm, no copies either way.
    void setWindowEvents(int32_t currentTime, uint32_t timeWindow,
                         std::vector<ActorEvent>&& events) {
        cachedTime_ = currentTime;
        cachedWindow_ = timeWindow;

        {
            FRAME_PROFILE_SCOPE("WindowEvents::MoveVec");
            windowEvents_.swap(events);
        }

        updateGuidMapsIfNeeded();
        valid_ = true;
        positionsValid_ = false;
        resourcesValid_ = false;
    }

    void setActorPositions(const std::unordered_map<std::string_view, InterpolatedPosition>& positions) {
        actorPositions_.clear();
        for (const auto& [guid, pos] : positions) {
            actorPositions_[guid] = pos;
        }
        positionsValid_ = true;
    }

    void setActorResources(const std::unordered_map<std::string_view, InterpolatedResource>& resources) {
        actorResources_.clear();
        for (const auto& [guid, res] : resources) {
            actorResources_[guid] = res;
        }
        resourcesValid_ = true;
    }

    // Only valid after setWindowEvents/setActorPositions
    const std::vector<ActorEvent>& getWindowEvents() const { return windowEvents_; }
    const std::unordered_map<std::string_view, std::string_view>& getGuidToName() const { return guidToName_; }
    const std::unordered_map<std::string_view, std::string_view>& getGuidToTruncatedName() const { return guidToTruncatedName_; }
    const std::unordered_map<std::string_view, ActorType>& getGuidToType() const { return guidToType_; }
    const std::unordered_map<std::string_view, InterpolatedPosition>& getActorPositions() const { return actorPositions_; }
    const std::unordered_map<std::string_view, InterpolatedResource>& getActorResources() const { return actorResources_; }

    bool hasPositions() const { return positionsValid_; }
    bool hasResources() const { return resourcesValid_; }
    int32_t getCachedTime() const { return cachedTime_; }
    uint32_t getCachedWindow() const { return cachedWindow_; }

private:
    void updateGuidMapsIfNeeded() {
        if (!guidMapsPopulated_) {
            FRAME_PROFILE_SCOPE("WindowEvents::MapUpdate");
            std::string_view lastGuid;

            for (const auto& event : windowEvents_) {
                if (event.source_guid == lastGuid) {
                    continue;
                }
                lastGuid = event.source_guid;

                if (guidToName_.find(event.source_guid) == guidToName_.end()) {
                    auto sv = event.source_guid;
                    guidToName_.emplace(sv, event.source_actor);
                    guidToType_.emplace(sv, event.actor_type);
                    guidToTruncatedName_.emplace(sv, event.source_actor.substr(
                        0, std::min<size_t>(3, event.source_actor.length())));
                }
            }
        }
    }

    bool valid_ = false;
    bool positionsValid_ = false;
    bool resourcesValid_ = false;
    bool guidMapsPopulated_ = false;  // Set true after initial load populates all GUID maps
    int32_t cachedTime_ = 0;
    uint32_t cachedWindow_ = 0;

    std::vector<ActorEvent> windowEvents_;
    std::unordered_map<std::string_view, std::string_view> guidToName_;
    std::unordered_map<std::string_view, std::string_view> guidToTruncatedName_;
    std::unordered_map<std::string_view, ActorType> guidToType_;
    std::unordered_map<std::string_view, InterpolatedPosition> actorPositions_;
    std::unordered_map<std::string_view, InterpolatedResource> actorResources_;
};
