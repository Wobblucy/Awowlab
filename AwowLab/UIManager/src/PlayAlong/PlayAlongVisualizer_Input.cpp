#include "PlayAlong/PlayAlongVisualizer.h"
#include "Camera/Camera3D.h"
#include "Database/SpellCastDatabase.h"
#include <imgui.h>
#include <windows.h>  // Beep()
#include <thread>

namespace PlayAlong {

void PlayAlongVisualizer::handleInput() {
    CloneInputState cloneInput = inputHandler_.processInput();
    float deltaTime = ImGui::GetIO().DeltaTime;

    // Toggle over-the-shoulder chase camera with V (ignore while typing).
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        camera3D_->setChaseMode(!camera3D_->isChaseMode());
    }

    // Apply mouse-look first so movement this frame uses the updated facing.
    // Right-mouse drag rotates the camera in both modes (WoW right-mouse steer).
    auto cameraInput = inputHandler_.getCameraInput();
    if (cameraInput.isRotating) {
        camera3D_->rotate(cameraInput.yawDelta, cameraInput.pitchDelta);
    }
    if (cameraInput.zoomDelta != 0.0f) {
        camera3D_->zoom(cameraInput.zoomDelta);
    }

    // Camera yaw drives the character's facing so mouse-look steers the clone
    // and W/S/A/D move relative to it.
    //  - Chase mode: camera looks along +yaw from behind, so forward == yaw.
    //  - Orbit mode: camera sits in the +yaw direction from the clone, so
    //    "forward" (away from camera) is yaw + PI, preserving the old feel.
    constexpr float PI = 3.14159265358979323846f;
    float facing = camera3D_->isChaseMode() ? camera3D_->getYaw()
                                            : camera3D_->getYaw() + PI;
    clone_.setFacing(facing);

    // Update clone movement (relative to the clone's own facing).
    clone_.update(deltaTime, cloneInput);

    // ESC asks for confirmation instead of leaving straight away
    if (inputHandler_.isEscapePressed() && !showExitConfirmation_) {
        showExitConfirmation_ = true;
        wasPlayingBeforeConfirmation_ = isPlaying_;
        isPlaying_ = false;
        exitConfirmationFrameDelay_ = 60;  // so the same ESC press doesn't close the popup
    }

    // Right Shift jumps to the next cast that has a keybind, moves the clone onto
    // the ghost and pauses
    if (inputHandler_.isSnapToPlayerPressed()) {
        uint32_t nextBoundTime = findNextBoundCastTime(currentTimeMs_);
        if (nextBoundTime > 0) {
            currentTimeMs_ = nextBoundTime;
        }

        float targetX, targetY, targetFacing;
        if (getTargetPlayerPosition(targetX, targetY, targetFacing)) {
            clone_.setPosition(targetX, targetY);
        }

        isPlaying_ = false;
        countdownActive_ = false;
    }
}

void PlayAlongVisualizer::updateCamera() {
    if (camera3D_->isChaseMode()) {
        // Over-the-shoulder: lock the camera behind the clone. Camera yaw (from
        // RMB drag) is the look direction and already drives the clone's facing.
        camera3D_->chaseFollow(clone_.getX(), clone_.getY());
    } else {
        // RTS/orbit: camera just tracks the clone's ground position.
        camera3D_->setTarget(clone_.getX(), clone_.getY(), 0.0f);
    }
}

void PlayAlongVisualizer::checkKeybindPresses() {
    if (!isPlaying_) return;
    if (!feedbackInitialized_) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    const auto& allKeybinds = keybindSettings_.getAllKeybinds();

    for (const auto& [spellId, keybind] : allKeybinds) {
        if (keybind.isUnbound()) continue;

        if (!keybindSettings_.isKeybindActive(spellId)) continue;

        bool keyPressed = ImGui::IsKeyPressed(static_cast<ImGuiKey>(keybind.imguiKey));
        bool modifiersMatch = (io.KeyCtrl == keybind.ctrl) &&
                              (io.KeyShift == keybind.shift) &&
                              (io.KeyAlt == keybind.alt);

        if (keyPressed && modifiersMatch) {
            // Record the key, not the spell, so several spells sharing a key all match
            feedbackSystem_.recordPlayerInput(keybind, static_cast<int32_t>(currentTimeMs_));
            break;  // Only record once per keypress
        }
    }
}

void PlayAlongVisualizer::playFeedbackSound(FeedbackResult result) {
    if (!audioFeedbackEnabled_) return;

    int frequency = 0;
    if (result == FeedbackResult::Good) {
        frequency = BEEP_GOOD_FREQ;
    } else if (result == FeedbackResult::Bad) {
        frequency = BEEP_BAD_FREQ;
    }
    // Neutral and Pending produce no sound
    if (frequency == 0) return;

    // Beep() blocks for its duration, so keep it off the render thread
    int duration = BEEP_DURATION_MS;
    std::thread([frequency, duration]() {
        Beep(frequency, duration);
    }).detach();
}

uint32_t PlayAlongVisualizer::findNextBoundCastTime(uint32_t afterTimeMs) const {
    if (!spellCastDb_ || targetPlayerGuid_.empty()) {
        return 0;
    }

    const auto& allCasts = spellCastDb_->getAllActorCasts();
    auto it = allCasts.find(targetPlayerGuid_);
    if (it == allCasts.end()) {
        return 0;
    }

    for (const auto& cast : it->second) {
        if (cast.end_timestamp_ms <= static_cast<int32_t>(afterTimeMs)) {
            continue;
        }

        if (keybindSettings_.hasKeybind(cast.spell_id) &&
            !keybindSettings_.isSpellIgnored(cast.spell_id)) {
            return static_cast<uint32_t>(cast.end_timestamp_ms);
        }
    }

    return 0;
}

} // namespace PlayAlong
