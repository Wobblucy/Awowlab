#pragma once

#include "PlayAlong/UserCloneActor.h"

struct ImGuiIO;

namespace PlayAlong {

// Input handling for Play Along mode, separate from MapVisualizer's
class PlayAlongInputHandler {
public:
    PlayAlongInputHandler() = default;

    // Reads this frame's ImGui input and returns the clone's movement state
    CloneInputState processInput();

    // Camera control state
    struct CameraInput {
        float yawDelta = 0.0f;      // Horizontal rotation (from RMB drag)
        float pitchDelta = 0.0f;    // Vertical rotation (from RMB drag)
        float zoomDelta = 0.0f;     // Zoom (from mouse wheel)
        bool isRotating = false;    // RMB is held down
    };

    // Get camera input for the current frame
    CameraInput getCameraInput() const { return cameraInput_; }

    // Check navigation keys
    bool isEscapePressed() const { return escapePressed_; }
    bool isSpacePressed() const { return spacePressed_; }
    bool isSnapToPlayerPressed() const { return snapToPlayerPressed_; }

private:
    CameraInput cameraInput_;
    bool escapePressed_ = false;
    bool spacePressed_ = false;
    bool snapToPlayerPressed_ = false;
    bool rightMouseDown_ = false;
};

} // namespace PlayAlong
