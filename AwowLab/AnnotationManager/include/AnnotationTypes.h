#pragma once

#include <string>
#include <cstdint>
#include <array>
#include <cstring>
#include "AnnotationMotionTypes.h"

// Rotations are stored in radians; the UI and trigger settings use degrees.
inline constexpr float kAnnotationDegToRad = 3.14159265f / 180.0f;
inline constexpr float kAnnotationRadToDeg = 180.0f / 3.14159265f;

// Shape types for annotations
enum class AnnotationShape : uint8_t {
    Circle = 0,      // Ground circle/cylinder (void zones, soaks)
    Rectangle,       // Rectangle (beams, zones)
    Cone,            // Frontal cone (cleaves)
    Line,            // Line segment (directional indicators)
    Star,            // Radial rays from center (debuff indicators)
    Donut,           // Ring shape with inner/outer radius (soak rings)
    COUNT
};

enum class AnnotationAnchor : uint8_t {
    World = 0,       // Fixed world position (x, y)
    Actor            // Follows an actor by GUID
};

enum class AnnotationDuration : uint8_t {
    Encounter = 0,   // Visible for entire encounter
    Timed            // Visible for X seconds from creation time
};

// Visual style for uncertainty indicators
enum class AnnotationVisualStyle : uint8_t {
    Solid = 0,       // Default solid fill
    Striped,         // Diagonal stripes (static uncertainty indicator)
    Hatched,         // Cross-hatch pattern (static uncertainty indicator)
    RippleRings,     // Hatched + animated ripples expanding outward (projectile waves)
    Swirl            // Static spiral stripes from center outward (saturation shift)
};

// World Marker types (WoW raid markers)
enum class WorldMarkerType : uint8_t {
    None = 0,        // Not a world marker (regular annotation)
    Star,            // Yellow star
    Circle,          // Orange circle
    Diamond,         // Purple diamond
    Triangle,        // Green triangle
    Moon,            // Silver/blue crescent moon
    Square,          // Blue square
    Cross,           // Red X
    Skull            // White skull
};

struct Annotation {
    uint64_t id = 0;

    // Visual properties
    AnnotationShape shape = AnnotationShape::Circle;
    std::array<float, 4> color = {1.0f, 0.0f, 0.0f, 0.5f};  // RGBA (default: semi-transparent red)

    // Shape-specific dimensions (yards, rotation in radians)
    // Circle: [0]=radius, [1]=height
    // Rectangle: [0]=width, [1]=length, [2]=height, [3]=rotation
    // Cone: [0]=length (radius), [1]=angle (degrees), [2]=height, [3]=rotation
    // Line: [0]=length, [1]=width, [2]=height, [3]=rotation
    // Star: [0]=ray length, [1]=ray width, [2]=height, [3]=rotation
    // Donut: [0]=outer radius, [1]=inner radius, [2]=height
    std::array<float, 4> dimensions = {10.0f, 0.5f, 0.0f, 0.0f};

    // Number of rays for Star shape (3-12)
    uint8_t pointCount = 6;

    // Anchor configuration
    AnnotationAnchor anchorMode = AnnotationAnchor::World;
    float worldX = 0.0f;                      // World anchor X position (target/destination)
    float worldY = 0.0f;                      // World anchor Y position
    std::string actorGuid;                    // Actor GUID (if anchorMode == Actor)

    // Source position (for Line shape: draws from source to target)
    float sourceX = 0.0f;                     // Source position X (caster)
    float sourceY = 0.0f;                     // Source position Y
    bool hasSourcePosition = false;           // True if sourceX/Y are valid

    // Duration configuration
    AnnotationDuration durationType = AnnotationDuration::Encounter;
    int32_t creationTimestamp_ms = 0;         // Timeline timestamp when created
    int32_t durationMs = 10000;               // Duration in ms (if durationType == Timed)
    bool skipFade = false;                    // If true, no fade at end (for continuous samples)

    // Sample lerping (for DuringDebuff continuous position updates)
    float nextX = 0.0f;                       // Next sample's X position
    float nextY = 0.0f;                       // Next sample's Y position
    bool hasNextPosition = false;             // True if nextX/Y valid (lerp toward them)

    // Visual style (for uncertainty indicators)
    AnnotationVisualStyle visualStyle = AnnotationVisualStyle::Solid;
    float stripeSpacing = 2.0f;               // Yards between stripes (for Striped/Hatched)
    float rippleSpeed = 5.0f;                 // Yards per second (for RippleRings)
    float rippleCount = 3.0f;                 // Number of visible ring cycles (for RippleRings)

    // Edge effect configuration
    bool enableEdgeFade = false;              // Soft alpha fade at edges
    bool enableGlow = false;                  // Brightness glow at edges
    bool enableOutline = false;               // Colored border around shape
    bool enableGradient = false;              // Color gradient center-to-edge
    float edgeFadeWidth = 0.15f;              // Normalized fade width (0.0-0.3)
    float outlineWidth = 0.05f;               // Normalized outline width (0.0-0.15)
    float glowIntensity = 1.0f;               // Glow strength (0.0-2.0)

    // Dual color support
    bool useSecondaryColor = false;           // Use secondary color for gradients/outlines/patterns
    bool useInnerZoneColor = false;           // Use inner zone color for center region
    std::array<float, 4> secondaryColor = {0.5f, 0.5f, 0.5f, 1.0f};  // Gray default
    std::array<float, 4> innerZoneColor = {0.0f, 1.0f, 0.0f, 0.5f};  // Green default (safe zone)

    // Motion configuration
    AnnotationMotionConfig motion;

    // World Marker configuration (if worldMarkerType != None)
    WorldMarkerType worldMarkerType = WorldMarkerType::None;
    float markerHeight = 4.0f;                // Height of floating symbol above ground (yards)

    // State
    bool visible = true;                      // User-toggled visibility
    bool deleted = false;                     // Marked for deletion (soft delete for potential undo)
    bool isAutoGenerated = false;             // True if created by auto-annotation trigger (not persisted)
    bool isPreview = false;                   // True if this is a preview annotation (shows direction lines)

    // Computed at runtime (not persisted)
    mutable float currentX = 0.0f;            // Resolved position X (after smoothing)
    mutable float currentY = 0.0f;            // Resolved position Y
    mutable bool positionValid = true;        // False if actor not visible

    // Position smoothing state (runtime only)
    mutable float targetX = 0.0f;             // Target position X (from interpolator)
    mutable float targetY = 0.0f;             // Target position Y
    mutable float prevX = 0.0f;               // Position at start of smoothing
    mutable float prevY = 0.0f;               // Position at start of smoothing
    mutable int32_t smoothStartTime_ms = 0;   // When smoothing started
    mutable bool hasValidTarget = false;      // True if target position is set

    bool isVisibleAtTime(int32_t currentTime_ms) const {
        if (!visible || deleted) return false;
        if (!positionValid) return false;

        if (durationType == AnnotationDuration::Encounter) {
            return true;
        }

        int32_t endTime = creationTimestamp_ms + durationMs;

        // Continuous samples (skipFade) use an inclusive end so consecutive
        // samples leave no gap; regular timed annotations use an exclusive end.
        if (skipFade) {
            return currentTime_ms >= creationTimestamp_ms && currentTime_ms <= endTime;
        }
        return currentTime_ms >= creationTimestamp_ms && currentTime_ms < endTime;
    }

    // 1.0 = fully visible, 0.0 = invisible. Fades over the last 20% of the
    // duration unless skipFade is set.
    float getFadeAtTime(int32_t currentTime_ms) const {
        if (durationType == AnnotationDuration::Encounter || skipFade) {
            return 1.0f;
        }

        int32_t endTime = creationTimestamp_ms + durationMs;
        int32_t fadeStartTime = creationTimestamp_ms + static_cast<int32_t>(durationMs * 0.8f);

        if (currentTime_ms < fadeStartTime) {
            return 1.0f;
        }
        if (currentTime_ms >= endTime) {
            return 0.0f;
        }

        float fadeProgress = static_cast<float>(currentTime_ms - fadeStartTime) /
                             static_cast<float>(endTime - fadeStartTime);
        return 1.0f - fadeProgress;
    }
};

// Key for per-encounter storage
struct AnnotationKey {
    uint32_t encounterId = 0;                 // WoW encounter ID
    std::string encounterName;                // Fallback for M+ dungeons (encounterId=0)
    std::string logFilePath;                  // Source log file path (for uniqueness)

    bool operator==(const AnnotationKey& other) const {
        return encounterId == other.encounterId &&
               encounterName == other.encounterName &&
               logFilePath == other.logFilePath;
    }
};

inline const char* annotationShapeToString(AnnotationShape shape) {
    switch (shape) {
        case AnnotationShape::Circle: return "circle";
        case AnnotationShape::Rectangle: return "rectangle";
        case AnnotationShape::Cone: return "cone";
        case AnnotationShape::Line: return "line";
        case AnnotationShape::Star: return "star";
        case AnnotationShape::Donut: return "donut";
        default: return "circle";
    }
}

inline AnnotationShape stringToAnnotationShape(const char* str) {
    if (strcmp(str, "rectangle") == 0) return AnnotationShape::Rectangle;
    if (strcmp(str, "cone") == 0) return AnnotationShape::Cone;
    if (strcmp(str, "line") == 0) return AnnotationShape::Line;
    if (strcmp(str, "star") == 0) return AnnotationShape::Star;
    if (strcmp(str, "donut") == 0) return AnnotationShape::Donut;
    return AnnotationShape::Circle;
}

inline const char* annotationAnchorToString(AnnotationAnchor anchor) {
    switch (anchor) {
        case AnnotationAnchor::World: return "world";
        case AnnotationAnchor::Actor: return "actor";
        default: return "world";
    }
}

inline AnnotationAnchor stringToAnnotationAnchor(const char* str) {
    if (strcmp(str, "actor") == 0) return AnnotationAnchor::Actor;
    return AnnotationAnchor::World;
}

inline const char* annotationDurationToString(AnnotationDuration duration) {
    switch (duration) {
        case AnnotationDuration::Encounter: return "encounter";
        case AnnotationDuration::Timed: return "timed";
        default: return "encounter";
    }
}

inline AnnotationDuration stringToAnnotationDuration(const char* str) {
    if (strcmp(str, "timed") == 0) return AnnotationDuration::Timed;
    return AnnotationDuration::Encounter;
}

inline const char* annotationVisualStyleToString(AnnotationVisualStyle style) {
    switch (style) {
        case AnnotationVisualStyle::Solid: return "solid";
        case AnnotationVisualStyle::Striped: return "striped";
        case AnnotationVisualStyle::Hatched: return "hatched";
        case AnnotationVisualStyle::RippleRings: return "ripple_rings";
        case AnnotationVisualStyle::Swirl: return "swirl";
        default: return "solid";
    }
}

inline AnnotationVisualStyle stringToAnnotationVisualStyle(const char* str) {
    if (strcmp(str, "striped") == 0) return AnnotationVisualStyle::Striped;
    if (strcmp(str, "hatched") == 0) return AnnotationVisualStyle::Hatched;
    if (strcmp(str, "ripple_rings") == 0) return AnnotationVisualStyle::RippleRings;
    if (strcmp(str, "swirl") == 0) return AnnotationVisualStyle::Swirl;
    return AnnotationVisualStyle::Solid;
}

inline const char* worldMarkerTypeToString(WorldMarkerType type) {
    switch (type) {
        case WorldMarkerType::Star: return "star";
        case WorldMarkerType::Circle: return "circle";
        case WorldMarkerType::Diamond: return "diamond";
        case WorldMarkerType::Triangle: return "triangle";
        case WorldMarkerType::Moon: return "moon";
        case WorldMarkerType::Square: return "square";
        case WorldMarkerType::Cross: return "cross";
        case WorldMarkerType::Skull: return "skull";
        default: return "none";
    }
}

inline WorldMarkerType stringToWorldMarkerType(const char* str) {
    if (strcmp(str, "star") == 0) return WorldMarkerType::Star;
    if (strcmp(str, "circle") == 0) return WorldMarkerType::Circle;
    if (strcmp(str, "diamond") == 0) return WorldMarkerType::Diamond;
    if (strcmp(str, "triangle") == 0) return WorldMarkerType::Triangle;
    if (strcmp(str, "moon") == 0) return WorldMarkerType::Moon;
    if (strcmp(str, "square") == 0) return WorldMarkerType::Square;
    if (strcmp(str, "cross") == 0) return WorldMarkerType::Cross;
    if (strcmp(str, "skull") == 0) return WorldMarkerType::Skull;
    return WorldMarkerType::None;
}

// RGB in 0-1
inline void getWorldMarkerColor(WorldMarkerType type, float& r, float& g, float& b) {
    switch (type) {
        case WorldMarkerType::Star:     r = 1.0f;   g = 0.82f;  b = 0.0f;   break;  // Yellow (255, 210, 0)
        case WorldMarkerType::Circle:   r = 1.0f;   g = 0.5f;   b = 0.0f;   break;  // Orange (255, 128, 0)
        case WorldMarkerType::Diamond:  r = 0.63f;  g = 0.13f;  b = 0.94f;  break;  // Purple (160, 32, 240)
        case WorldMarkerType::Triangle: r = 0.0f;   g = 1.0f;   b = 0.0f;   break;  // Green (0, 255, 0)
        case WorldMarkerType::Moon:     r = 0.75f;  g = 0.75f;  b = 1.0f;   break;  // Silver/blue (192, 192, 255)
        case WorldMarkerType::Square:   r = 0.0f;   g = 0.63f;  b = 1.0f;   break;  // Blue (0, 160, 255)
        case WorldMarkerType::Cross:    r = 1.0f;   g = 0.25f;  b = 0.25f;  break;  // Red (255, 64, 64)
        case WorldMarkerType::Skull:    r = 1.0f;   g = 1.0f;   b = 1.0f;   break;  // White (255, 255, 255)
        default:                        r = 1.0f;   g = 1.0f;   b = 1.0f;   break;  // Default white
    }
}
