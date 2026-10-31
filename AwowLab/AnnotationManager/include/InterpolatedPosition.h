#pragma once
#include <cstdint>

// Output of ActorPositionInterpolator, passed to AnnotationManager::updatePositions()
struct InterpolatedPosition {
    float x;
    float y;
    float facing;                // Radians, 0 to 2*pi
    bool hasPosition;
    bool hasFacing;              // True if facing data is valid
    int32_t lastEventTimestamp;  // Negative = before encounter start
    float scaleOverride = 1.0f;  // Per-actor scale multiplier (PlayAlong uses 1.3 for clone/ghost)
    float opacity = 1.0f;        // Per-actor opacity (0.0-1.0, PlayAlong uses 0.5 for other players)
    bool orientedShape = false;  // If true, built-in shape rotates to the actor's facing
                                 // instead of billboarding to the camera (PlayAlong clone/ghost)
};
