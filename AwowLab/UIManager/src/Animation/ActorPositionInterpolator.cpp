#include "Animation/ActorPositionInterpolator.h"
#include "Animation/AngleMath.h"
#include "../../Database/OutputWriter.h"
#include "Core/FrameProfiler.h"
#include <algorithm>
#include <cmath>

namespace {
    using angle_math::lerpAngle;

    // Facing is stored as radians * 10000
    inline float facingToRadians(uint16_t facing) {
        return static_cast<float>(facing) / 10000.0f;
    }
}

void ActorPositionInterpolator::reset() {
    cachedPositions_.clear();
    lastWindowStart_ = 0;
    lastWindowEnd_ = 0;
    lastEventCount_ = 0;
}

void ActorPositionInterpolator::updateCachedPositions(const std::vector<ActorEvent>& windowEvents) {
    if (windowEvents.empty()) {
        cachedPositions_.clear();
        lastEventCount_ = 0;
        return;
    }

    int32_t windowStart = windowEvents.front().timestamp_ms;
    int32_t windowEnd = windowEvents.back().timestamp_ms;

    bool needsRebuild = (windowEvents.size() != lastEventCount_) ||
                        (windowStart != lastWindowStart_) ||
                        (windowEnd != lastWindowEnd_);

    if (!needsRebuild) {
        return;
    }

    FRAME_PROFILE_SCOPE("Interpolate::Build");

    cachedPositions_.clear();

    struct PositionAccumulator {
        float sumX = 0;
        float sumY = 0;
        float meanFacing = 0;  // Running mean taken the short way round the circle
        int count = 0;
    };

    // Append per actor and sort once afterwards, cheaper than a std::map per actor
    std::unordered_map<std::string_view, std::vector<std::pair<int32_t, PositionAccumulator>>> actorTimestampPositions;
    actorTimestampPositions.reserve(100);

    for (const auto& event : windowEvents) {
        auto& actorVec = actorTimestampPositions[event.source_guid];

        // Events sharing a timestamp are averaged
        if (!actorVec.empty() && actorVec.back().first == event.timestamp_ms) {
            auto& accum = actorVec.back().second;
            accum.sumX += event.x_loc;
            accum.sumY += event.y_loc;
            accum.count++;
            accum.meanFacing = angle_math::addToAngleMean(
                accum.meanFacing, facingToRadians(event.facing_radians), accum.count);
        } else {
            PositionAccumulator accum;
            accum.sumX = event.x_loc;
            accum.sumY = event.y_loc;
            accum.count = 1;
            accum.meanFacing = angle_math::addToAngleMean(
                0.0f, facingToRadians(event.facing_radians), accum.count);
            actorVec.emplace_back(event.timestamp_ms, accum);
        }
    }

    for (auto& [actor, timestampVec] : actorTimestampPositions) {
        std::sort(timestampVec.begin(), timestampVec.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        auto& positions = cachedPositions_[actor];
        positions.reserve(timestampVec.size());

        for (const auto& [timestamp, accum] : timestampVec) {
            AveragedPosition avgPos;
            float invCount = 1.0f / accum.count;
            avgPos.x = accum.sumX * invCount;
            avgPos.y = accum.sumY * invCount;
            // A plain average of 6.2 and 0.1 lands near pi, facing the
            // actor backwards; the running mean already handled the wrap
            avgPos.facing = accum.meanFacing;
            avgPos.hasFacing = true;
            avgPos.timestamp = timestamp;
            positions.push_back(avgPos);
        }
    }

    lastWindowStart_ = windowStart;
    lastWindowEnd_ = windowEnd;
    lastEventCount_ = windowEvents.size();
}

void ActorPositionInterpolator::findBracketingPositions(
    const std::vector<AveragedPosition>& positions,
    int32_t currentTime,
    AveragedPosition& beforePos,
    AveragedPosition* afterPos,
    bool& hasBefore,
    bool& hasAfter
) {
    hasBefore = false;
    hasAfter = false;

    for (const auto& pos : positions) {
        if (pos.timestamp <= currentTime) {
            if (!hasBefore || pos.timestamp > beforePos.timestamp) {
                beforePos = pos;
                hasBefore = true;
            }
        }

        if (pos.timestamp > currentTime) {
            if (!hasAfter || pos.timestamp < afterPos->timestamp) {
                *afterPos = pos;
                hasAfter = true;
            }
        }
    }
}

InterpolatedPosition ActorPositionInterpolator::lerp(
    const AveragedPosition& beforePos,
    const AveragedPosition* afterPos,
    int32_t currentTime
) {
    InterpolatedPosition result;
    result.hasPosition = true;
    result.hasFacing = beforePos.hasFacing;
    result.lastEventTimestamp = beforePos.timestamp;

    if (afterPos != nullptr) {
        int32_t timeDiff = afterPos->timestamp - beforePos.timestamp;
        if (timeDiff > 0) {
            float t = static_cast<float>(currentTime - beforePos.timestamp) / static_cast<float>(timeDiff);
            t = std::clamp(t, 0.0f, 1.0f);

            result.x = beforePos.x + t * (afterPos->x - beforePos.x);
            result.y = beforePos.y + t * (afterPos->y - beforePos.y);
            result.facing = lerpAngle(beforePos.facing, afterPos->facing, t);
            result.hasFacing = true;

            if (t > 0.5f) {
                result.lastEventTimestamp = afterPos->timestamp;
            }
        } else {
            result.x = beforePos.x;
            result.y = beforePos.y;
            result.facing = beforePos.facing;
        }
    } else {
        result.x = beforePos.x;
        result.y = beforePos.y;
        result.facing = beforePos.facing;
    }

    return result;
}

std::unordered_map<std::string_view, InterpolatedPosition>
ActorPositionInterpolator::interpolatePositions(
    const std::vector<ActorEvent>& windowEvents,
    int32_t currentTime,
    int32_t inactivityThresholdMs
) {
    std::unordered_map<std::string_view, InterpolatedPosition> actorPositions;
    interpolatePositionsInto(actorPositions, windowEvents, currentTime, inactivityThresholdMs);
    return actorPositions;
}

void ActorPositionInterpolator::interpolatePositionsInto(
    std::unordered_map<std::string_view, InterpolatedPosition>& outPositions,
    const std::vector<ActorEvent>& windowEvents,
    int32_t currentTime,
    int32_t inactivityThresholdMs
) {
    outPositions.clear();

    updateCachedPositions(windowEvents);

    {
        FRAME_PROFILE_SCOPE("Interpolate::Lerp");
        for (const auto& [actor, positions] : cachedPositions_) {
            if (positions.empty()) {
                continue;
            }

            AveragedPosition beforePos;
            AveragedPosition afterPos;
            bool hasBefore, hasAfter;

            findBracketingPositions(positions, currentTime, beforePos, &afterPos, hasBefore, hasAfter);

            if (!hasBefore) {
                continue;
            }

            if (currentTime > beforePos.timestamp &&
                (currentTime - beforePos.timestamp) > inactivityThresholdMs) {
                continue;
            }

            InterpolatedPosition interpPos = lerp(beforePos, hasAfter ? &afterPos : nullptr, currentTime);
            outPositions[actor] = interpPos;
        }
    }
}
