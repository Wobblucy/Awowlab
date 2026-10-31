#pragma once

#include "AnnotationMotionTypes.h"
#include <cstdint>
#include <vector>

struct Annotation;

struct RaindropInstance {
    float x;        // World X position
    float y;        // World Y position
    float alpha;    // Opacity (1.0 = fully visible, fades to 0)
};

// Motion for Rotate, Projectile and Raindrops annotations
class AnnotationMotionCalculator {
public:
    // Position, rotation and visibility at the given timeline time
    static MotionState calculate(
        const Annotation& annotation,
        int32_t currentTime_ms
    );

    // Every raindrop alive at the given time
    static std::vector<RaindropInstance> calculateRaindrops(
        const Annotation& annotation,
        int32_t currentTime_ms
    );

private:
    // Rotates around the root point
    static MotionState calculateRotate(
        const Annotation& annotation,
        int32_t elapsed_ms
    );

    // Moves from the start to the end offset
    static MotionState calculateProjectile(
        const Annotation& annotation,
        int32_t elapsed_ms
    );

    // Deterministic, so a drop stays put when the timeline is scrubbed; returns [0,1)
    static float hashToFloat(uint32_t seed);

    static void randomPositionInCircle(
        uint32_t seed, float centerX, float centerY, float radius,
        float& outX, float& outY
    );

    // The rectangle may be rotated
    static void randomPositionInRectangle(
        uint32_t seed, float centerX, float centerY,
        float width, float length, float rotation,
        float& outX, float& outY
    );
};
