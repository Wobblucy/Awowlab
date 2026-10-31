#include "PlayAlong/PlayAlongInputHandler.h"
#include <imgui.h>

namespace PlayAlong {

CloneInputState PlayAlongInputHandler::processInput() {
    CloneInputState state;
    ImGuiIO& io = ImGui::GetIO();

    bool imguiWantsKeyboard = io.WantCaptureKeyboard;
    bool imguiWantsMouse = io.WantCaptureMouse;

    escapePressed_ = false;
    spacePressed_ = false;
    snapToPlayerPressed_ = false;
    cameraInput_ = CameraInput{};

    // ESC should always work to exit, regardless of ImGui focus
    escapePressed_ = ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (!imguiWantsKeyboard) {
        // WASD movement (relative to camera direction)
        state.forward = ImGui::IsKeyDown(ImGuiKey_W);
        state.backward = ImGui::IsKeyDown(ImGuiKey_S);
        state.strafeLeft = ImGui::IsKeyDown(ImGuiKey_A);
        state.strafeRight = ImGui::IsKeyDown(ImGuiKey_D);

        spacePressed_ = ImGui::IsKeyPressed(ImGuiKey_Space);

        snapToPlayerPressed_ = ImGui::IsKeyPressed(ImGuiKey_RightShift);
    }

    if (!imguiWantsMouse) {
        if (io.MouseDown[1]) {  // Right mouse button
            if (!rightMouseDown_) {
                rightMouseDown_ = true;
            }
            cameraInput_.isRotating = true;
            cameraInput_.yawDelta = -io.MouseDelta.x * 0.005f;
            cameraInput_.pitchDelta = io.MouseDelta.y * 0.005f;  // Inverted for natural feel
        } else {
            rightMouseDown_ = false;
            cameraInput_.isRotating = false;
        }

        if (io.MouseWheel != 0.0f) {
            cameraInput_.zoomDelta = -io.MouseWheel * 5.0f;
        }
    }

    return state;
}

} // namespace PlayAlong
