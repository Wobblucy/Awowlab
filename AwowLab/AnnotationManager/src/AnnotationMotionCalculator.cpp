#include "AnnotationMotionCalculator.h"
#include "AnnotationTypes.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MotionState AnnotationMotionCalculator::calculate(
    const Annotation& annotation,
    int32_t currentTime_ms
) {
    int32_t elapsed_ms = currentTime_ms - annotation.creationTimestamp_ms;
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }

    switch (annotation.motion.type) {
        case AnnotationMotionType::Rotate:
            return calculateRotate(annotation, elapsed_ms);

        case AnnotationMotionType::Projectile:
            return calculateProjectile(annotation, elapsed_ms);

        case AnnotationMotionType::None:
        default:
            return {
                annotation.currentX,
                annotation.currentY,
                annotation.dimensions[3],
                true
            };
    }
}

MotionState AnnotationMotionCalculator::calculateRotate(
    const Annotation& annotation,
    int32_t elapsed_ms
) {
    float elapsed_s = static_cast<float>(elapsed_ms) / 1000.0f;

    float radiansPerSec = annotation.motion.rotationSpeed_degPerSec * static_cast<float>(M_PI / 180.0);

    float baseRotation = annotation.dimensions[3];
    float accumulatedRotation = elapsed_s * radiansPerSec;
    float currentRotation = baseRotation + accumulatedRotation;

    return {
        annotation.currentX,
        annotation.currentY,
        currentRotation,
        true
    };
}

MotionState AnnotationMotionCalculator::calculateProjectile(
    const Annotation& annotation,
    int32_t elapsed_ms
) {
    // Can exceed 1.0 once the projectile has arrived
    float progress = 0.0f;
    if (annotation.motion.travelTime_ms > 0) {
        progress = static_cast<float>(elapsed_ms) / static_cast<float>(annotation.motion.travelTime_ms);
    } else {
        progress = 1.0f;
    }

    float startX = annotation.worldX;
    float startY = annotation.worldY;
    float endX = startX + annotation.motion.endOffsetX;
    float endY = startY + annotation.motion.endOffsetY;

    float currentX, currentY;
    bool visible = true;

    if (annotation.motion.returnToOrigin) {
        // Out in the first half of travelTime, back in the second
        float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
        float lerp;
        if (clampedProgress <= 0.5f) {
            lerp = clampedProgress * 2.0f;
        } else {
            lerp = 2.0f - (clampedProgress * 2.0f);
        }
        currentX = startX + (endX - startX) * lerp;
        currentY = startY + (endY - startY) * lerp;

        if (progress >= 1.0f && annotation.motion.endBehavior == ProjectileEndBehavior::Disappear) {
            visible = false;
        }
    } else {
        float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
        currentX = startX + (endX - startX) * clampedProgress;
        currentY = startY + (endY - startY) * clampedProgress;

        if (progress >= 1.0f && annotation.motion.endBehavior == ProjectileEndBehavior::Disappear) {
            visible = false;
        }
    }

    return {
        currentX,
        currentY,
        annotation.dimensions[3],
        visible
    };
}

std::vector<RaindropInstance> AnnotationMotionCalculator::calculateRaindrops(
    const Annotation& annotation,
    int32_t currentTime_ms
) {
    std::vector<RaindropInstance> drops;

    if (annotation.motion.type != AnnotationMotionType::Raindrops) {
        return drops;
    }

    int32_t elapsed_ms = currentTime_ms - annotation.creationTimestamp_ms;
    if (elapsed_ms < 0) {
        return drops;
    }

    float elapsed_s = static_cast<float>(elapsed_ms) / 1000.0f;
    float dropsPerSec = annotation.motion.dropsPerSecond;
    float dropLifetime_s = static_cast<float>(annotation.motion.dropLifetime_ms) / 1000.0f;

    if (dropsPerSec <= 0.0f || dropLifetime_s <= 0.0f) {
        return drops;
    }

    int32_t totalDrops = static_cast<int32_t>(elapsed_s * dropsPerSec);

    // Drops older than their lifetime are gone
    int32_t maxVisibleDrops = static_cast<int32_t>(std::ceil(dropLifetime_s * dropsPerSec)) + 1;
    int32_t firstVisibleDrop = std::max(0, totalDrops - maxVisibleDrops);

    constexpr int32_t MAX_DROPS = 40;
    if (totalDrops - firstVisibleDrop > MAX_DROPS) {
        firstVisibleDrop = totalDrops - MAX_DROPS;
    }

    drops.reserve(static_cast<size_t>(totalDrops - firstVisibleDrop));

    for (int32_t dropIndex = firstVisibleDrop; dropIndex < totalDrops; ++dropIndex) {
        float spawnTime_s = static_cast<float>(dropIndex) / dropsPerSec;
        float dropAge_s = elapsed_s - spawnTime_s;

        if (dropAge_s > dropLifetime_s) {
            continue;
        }

        float alpha = 1.0f - (dropAge_s / dropLifetime_s);
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        // Seeded by drop index so a drop keeps its position when the timeline
        // is scrubbed; the annotation ID keeps annotations from matching.
        uint32_t seed = static_cast<uint32_t>(dropIndex) * 2654435761u + static_cast<uint32_t>(annotation.id);

        float dropX, dropY;

        switch (annotation.shape) {
            case AnnotationShape::Rectangle:
                randomPositionInRectangle(
                    seed,
                    annotation.currentX, annotation.currentY,
                    annotation.dimensions[0],
                    annotation.dimensions[1],
                    annotation.dimensions[3],
                    dropX, dropY
                );
                break;

            case AnnotationShape::Circle:
            default:
                // Other shapes are treated as a circle of radius dimensions[0]
                randomPositionInCircle(
                    seed,
                    annotation.currentX, annotation.currentY,
                    annotation.dimensions[0],
                    dropX, dropY
                );
                break;
        }

        drops.push_back({dropX, dropY, alpha});
    }

    return drops;
}

float AnnotationMotionCalculator::hashToFloat(uint32_t seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed = seed + (seed << 3);
    seed = seed ^ (seed >> 4);
    seed = seed * 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return static_cast<float>(seed) / static_cast<float>(0xFFFFFFFF);
}

void AnnotationMotionCalculator::randomPositionInCircle(
    uint32_t seed, float centerX, float centerY, float radius,
    float& outX, float& outY
) {
    // sqrt keeps the distribution uniform over the area
    float r = radius * std::sqrt(hashToFloat(seed));
    float theta = hashToFloat(seed + 1) * 2.0f * static_cast<float>(M_PI);

    outX = centerX + r * std::cos(theta);
    outY = centerY + r * std::sin(theta);
}

void AnnotationMotionCalculator::randomPositionInRectangle(
    uint32_t seed, float centerX, float centerY,
    float width, float length, float rotation,
    float& outX, float& outY
) {
    float localX = (hashToFloat(seed) - 0.5f) * width;
    float localY = (hashToFloat(seed + 1) - 0.5f) * length;

    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);

    outX = centerX + localX * cosR - localY * sinR;
    outY = centerY + localX * sinR + localY * cosR;
}
