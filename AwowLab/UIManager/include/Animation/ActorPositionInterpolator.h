#pragma once
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include "AnnotationManager/include/InterpolatedPosition.h"

struct ActorEvent;

struct InterpolatedResource {
    float healthPercent;
    uint64_t currentHealth;
    uint64_t maxHealth;
    float powerPercent;
    uint32_t currentPower;
    uint32_t maxPower;
    int8_t powerType;
    bool hasResource;
};

struct AveragedPosition {
    float x;
    float y;
    float facing;       // radians in [0, 2pi), averaged on the circle
    int32_t timestamp;  // negative = before encounter start
    bool hasFacing;
};

class ActorPositionInterpolator {
public:
    ActorPositionInterpolator() = default;

    void reset();

    std::unordered_map<std::string_view, InterpolatedPosition> interpolatePositions(
        const std::vector<ActorEvent>& windowEvents,
        int32_t currentTime,
        int32_t inactivityThresholdMs = 5000
    );

    void interpolatePositionsInto(
        std::unordered_map<std::string_view, InterpolatedPosition>& outPositions,
        const std::vector<ActorEvent>& windowEvents,
        int32_t currentTime,
        int32_t inactivityThresholdMs = 5000
    );

private:
    // Kept between frames; rebuilt when the event window changes
    std::unordered_map<std::string_view, std::vector<AveragedPosition>> cachedPositions_;

    // Identify the window the cache was built from
    int32_t lastWindowStart_ = 0;
    int32_t lastWindowEnd_ = 0;
    size_t lastEventCount_ = 0;

    void updateCachedPositions(const std::vector<ActorEvent>& windowEvents);

    void findBracketingPositions(
        const std::vector<AveragedPosition>& positions,
        int32_t currentTime,
        AveragedPosition& beforePos,
        AveragedPosition* afterPos,
        bool& hasBefore,
        bool& hasAfter
    );

    InterpolatedPosition lerp(
        const AveragedPosition& beforePos,
        const AveragedPosition* afterPos,
        int32_t currentTime
    );
};
