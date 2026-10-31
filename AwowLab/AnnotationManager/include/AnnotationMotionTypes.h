#pragma once

#include <cstdint>
#include <cstring>

// Motion type for annotations (mutually exclusive)
enum class AnnotationMotionType : uint8_t {
    None = 0,       // Static annotation (default)
    Rotate,         // Continuous rotation around root point
    Projectile,     // Linear movement from start to end offset
    Raindrops       // Spawns small circles within annotation bounds
};

// What happens when projectile reaches destination
enum class ProjectileEndBehavior : uint8_t {
    Persist = 0,    // Annotation stays at end position
    Disappear       // Annotation disappears when reaching destination
};

// How projectile end position is determined
enum class ProjectileEndMode : uint8_t {
    Manual = 0,         // Use endOffsetX/Y directly
    ToTarget,           // Travel from caster to spell target position (snapshotted at cast time)
    InFacingDirection   // Travel in caster's facing direction for configured distance
};

// Motion configuration (embedded in Annotation and AutoAnnotationTrigger)
struct AnnotationMotionConfig {
    AnnotationMotionType type = AnnotationMotionType::None;

    // Rotate parameters
    float rotationSpeed_degPerSec = 45.0f;  // Degrees per second (positive = clockwise)

    // Projectile parameters
    float endOffsetX = 0.0f;                // End X offset from start (yards) - for Manual mode or fallback
    float endOffsetY = 0.0f;                // End Y offset from start (yards) - for Manual mode or fallback
    int32_t travelTime_ms = 3000;           // Travel duration in milliseconds
    ProjectileEndBehavior endBehavior = ProjectileEndBehavior::Persist;
    ProjectileEndMode endMode = ProjectileEndMode::Manual;  // How end position is determined
    float projectileDistance = 40.0f;       // Distance in yards (for InFacingDirection mode)
    float facingOffset_deg = 0.0f;          // Degree offset for InFacingDirection (-180 to 180)
    bool returnToOrigin = false;            // If true, projectile returns to start (splits travelTime in half)

    // Raindrops parameters
    float dropsPerSecond = 5.0f;            // Spawn frequency (1-20)
    float dropRadius = 2.5f;                // Size of each drop in yards (0.5-5.0)
    int32_t dropLifetime_ms = 1000;         // How long each drop is visible (500-2000)
};

// Result of motion calculation
struct MotionState {
    float posX;         // Current X position
    float posY;         // Current Y position
    float rotation;     // Current rotation in radians
    bool visible;       // False if projectile disappeared at destination
};

inline const char* annotationMotionTypeToString(AnnotationMotionType type) {
    switch (type) {
        case AnnotationMotionType::None: return "none";
        case AnnotationMotionType::Rotate: return "rotate";
        case AnnotationMotionType::Projectile: return "projectile";
        case AnnotationMotionType::Raindrops: return "raindrops";
        default: return "none";
    }
}

inline AnnotationMotionType stringToAnnotationMotionType(const char* str) {
    if (std::strcmp(str, "rotate") == 0) return AnnotationMotionType::Rotate;
    if (std::strcmp(str, "projectile") == 0) return AnnotationMotionType::Projectile;
    if (std::strcmp(str, "raindrops") == 0) return AnnotationMotionType::Raindrops;
    return AnnotationMotionType::None;
}

inline const char* projectileEndBehaviorToString(ProjectileEndBehavior behavior) {
    switch (behavior) {
        case ProjectileEndBehavior::Persist: return "persist";
        case ProjectileEndBehavior::Disappear: return "disappear";
        default: return "persist";
    }
}

inline ProjectileEndBehavior stringToProjectileEndBehavior(const char* str) {
    if (std::strcmp(str, "disappear") == 0) return ProjectileEndBehavior::Disappear;
    return ProjectileEndBehavior::Persist;
}

inline const char* projectileEndModeToString(ProjectileEndMode mode) {
    switch (mode) {
        case ProjectileEndMode::Manual: return "manual";
        case ProjectileEndMode::ToTarget: return "to_target";
        case ProjectileEndMode::InFacingDirection: return "in_facing";
        default: return "manual";
    }
}

inline ProjectileEndMode stringToProjectileEndMode(const char* str) {
    if (std::strcmp(str, "to_target") == 0) return ProjectileEndMode::ToTarget;
    if (std::strcmp(str, "in_facing") == 0) return ProjectileEndMode::InFacingDirection;
    return ProjectileEndMode::Manual;
}
