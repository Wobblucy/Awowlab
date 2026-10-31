#pragma once
#include <unordered_map>
#include <string>
#include <string_view>
#include <optional>

class Camera3D;
struct InterpolatedPosition;

// Handles dynamic camera control (following actors, calculating centroid)
class CameraController {
public:
    CameraController() = default;

    // Follows the focused actor, or the centroid of the players when none is focused.
    // followSpeed runs from about 1 (slow) to 10 (snappy).
    void updateCameraTarget(
        Camera3D* camera,
        const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
        const std::optional<std::string_view>& focusedActorGuid = std::nullopt,
        float deltaTime = 0.016f,
        float followSpeed = 3.0f
    );

private:
    bool calculateCentroid(
        const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
        float& outX,
        float& outY
    );
};
