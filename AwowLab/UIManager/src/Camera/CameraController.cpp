#include "Camera/CameraController.h"
#include "Camera/Camera3D.h"
#include "Animation/ActorPositionInterpolator.h"
#include <cmath>
#include <string_view>

void CameraController::updateCameraTarget(
    Camera3D* camera,
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    const std::optional<std::string_view>& focusedActorGuid,
    float deltaTime,
    float followSpeed
) {
    if (!camera || actorPositions.empty()) {
        return;
    }

    float targetX = 0.0f, targetY = 0.0f;
    bool hasTarget = false;

    if (focusedActorGuid.has_value()) {
        auto it = actorPositions.find(focusedActorGuid.value());
        if (it != actorPositions.end() && it->second.hasPosition) {
            targetX = it->second.x;
            targetY = it->second.y;

            // Offset the target from the actor, scaled by cos(pitch) and zoom:
            // none when looking straight down, most when looking level, and
            // smaller when zoomed in so the actor stays on screen
            constexpr float kBaseOffset = 20.0f;
            constexpr float kReferenceDistance = 100.0f;

            float yaw = camera->getYaw();
            float pitch = camera->getPitch();
            float distance = camera->getDistance();

            float pitchFactor = std::cos(pitch);
            float distanceFactor = distance / kReferenceDistance;
            float offset = kBaseOffset * pitchFactor * distanceFactor;

            // Along +yaw, the side of the target the camera sits on
            targetX += offset * std::cos(yaw);
            targetY += offset * std::sin(yaw);

            hasTarget = true;
        }
    }

    // No focused actor with a position: use the centroid
    if (!hasTarget) {
        hasTarget = calculateCentroid(actorPositions, targetX, targetY);
    }

    if (hasTarget) {
        // Exponential smoothing, independent of frame rate. At speed 3 the
        // camera closes ~63% of the gap in 0.33 s.
        float smoothingAlpha = 1.0f - std::exp(-followSpeed * deltaTime);
        smoothingAlpha = std::min(smoothingAlpha, 1.0f);

        camera->smoothTargetTo(targetX, targetY, 0.0f, smoothingAlpha);
    }
}

bool CameraController::calculateCentroid(
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    float& outX,
    float& outY
) {
    if (actorPositions.empty()) {
        return false;
    }

    float sumX = 0.0f;
    float sumY = 0.0f;
    int count = 0;

    // Players only, falling back to every actor when there are none
    constexpr std::string_view kPlayerPrefix = "Player-";

    for (const auto& [guid, pos] : actorPositions) {
        if (pos.hasPosition && guid.compare(0, kPlayerPrefix.size(), kPlayerPrefix) == 0) {
            sumX += pos.x;
            sumY += pos.y;
            count++;
        }
    }

    if (count == 0) {
        for (const auto& [guid, pos] : actorPositions) {
            if (pos.hasPosition) {
                sumX += pos.x;
                sumY += pos.y;
                count++;
            }
        }
    }

    if (count == 0) {
        return false;
    }

    outX = sumX / count;
    outY = sumY / count;
    return true;
}
