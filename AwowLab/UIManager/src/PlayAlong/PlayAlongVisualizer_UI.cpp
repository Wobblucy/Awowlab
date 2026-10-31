
#include "PlayAlong/PlayAlongVisualizer.h"
#include "Database/SpellCastDatabase.h"
#include "AutoAnnotationTriggerManager.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <cmath>

namespace PlayAlong {

void PlayAlongVisualizer::renderUI() {
    renderPlaybackControls();
    renderPositionInfo();

    if (!targetPlayerGuid_.empty()) {
        combinedGcd_.render(targetPlayerGuid_, static_cast<int32_t>(currentTimeMs_),
                           spellCastDb_, spellIconLoader_,
                           feedbackInitialized_ ? &feedbackSystem_ : nullptr);
    }

    if (feedbackInitialized_) {
        statsPanel_.render(&feedbackSystem_);
    }

    if (countdownActive_) {
        renderCountdownOverlay();
    }

    if (snapHintTimeRemaining_ > 0.0f) {
        renderSnapHint();
    }

    if (showExitConfirmation_) {
        ImGui::OpenPopup("##ExitConfirmation");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##ExitConfirmation", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (exitConfirmationFrameDelay_ > 0) {
            exitConfirmationFrameDelay_--;
        }

        ImGui::TextUnformatted(L("playalong.return_home_confirm"));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 80.0f * ImGui::GetIO().FontGlobalScale;
        float totalWidth = buttonWidth * 2 + ImGui::GetStyle().ItemSpacing.x;
        float startX = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;
        ImGui::SetCursorPosX(startX);

        if (ImGui::Button(L("btn.yes"), ImVec2(buttonWidth, 0))) {
            returnToLandingPage_ = true;
            showExitConfirmation_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        // Only allow ESC to close after frame delay expires
        bool escapeClose = (exitConfirmationFrameDelay_ == 0) && ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (ImGui::Button(L("btn.no"), ImVec2(buttonWidth, 0)) || escapeClose) {
            showExitConfirmation_ = false;
            isPlaying_ = wasPlayingBeforeConfirmation_;  // Restore previous play state
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void PlayAlongVisualizer::renderPlaybackControls() {
    float scale = ImGui::GetIO().FontGlobalScale;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float panelWidth = 500.0f * scale;
    float panelHeight = 90.0f * scale;

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + (viewport->Size.x - panelWidth) * 0.5f,
               viewport->Pos.y + viewport->Size.y - panelHeight - 80.0f * scale));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.15f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##PlaybackControls", nullptr, flags)) {
        ImGui::SetCursorPosX(15.0f * scale);
        ImGui::SetCursorPosY(15.0f * scale);

        const char* playLabel;
        if (countdownActive_) {
            playLabel = L("btn.pause");  // Show pause during countdown
        } else {
            playLabel = isPlaying_ ? L("btn.pause") : L("btn.play");
        }

        if (ImGui::Button(playLabel, ImVec2(80.0f * scale, 30.0f * scale))) {
            if (countdownActive_) {
                countdownActive_ = false;
            } else if (isPlaying_) {
                isPlaying_ = false;
            } else {
                countdownActive_ = true;
                countdownTimeRemaining_ = COUNTDOWN_DURATION;
                ImGui::SetKeyboardFocusHere(-1);  // Clear focus so WASD works immediately
            }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(panelWidth - 130.0f * scale);

        int timeMs = static_cast<int>(currentTimeMs_);
        int minMs = static_cast<int>(minTimeMs_);
        int maxMs = static_cast<int>(maxTimeMs_);

        if (ImGui::SliderInt("##Timeline", &timeMs, minMs, maxMs, "")) {
            currentTimeMs_ = static_cast<uint32_t>(timeMs);
        }

        ImGui::SetCursorPosX(15.0f * scale);
        int seconds = (currentTimeMs_ - minTimeMs_) / 1000;
        int minutes = seconds / 60;
        seconds = seconds % 60;

        int totalSeconds = (maxTimeMs_ - minTimeMs_) / 1000;
        int totalMinutes = totalSeconds / 60;
        totalSeconds = totalSeconds % 60;

        ImGui::Text("%d:%02d / %d:%02d", minutes, seconds, totalMinutes, totalSeconds);

        // Audio feedback toggle on the right side
        ImGui::SameLine(panelWidth - 120.0f * scale);
        ImGui::Checkbox(L("playalong.audio_feedback"), &audioFeedbackEnabled_);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void PlayAlongVisualizer::renderPositionInfo() {
    float scale = ImGui::GetIO().FontGlobalScale;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 20.0f * scale,
                                    viewport->Pos.y + 20.0f * scale));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.15f, 0.90f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##PositionInfo", nullptr, flags)) {

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
        ImGui::Text("%s: %s", L("playalong.practicing_as"), targetPlayerName_.c_str());
        ImGui::PopStyleColor();

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.80f, 0.40f, 1.0f));
        ImGui::Text("%s: (%.1f, %.1f)", L("playalong.your_position"),
                    clone_.getX(), clone_.getY());
        ImGui::PopStyleColor();

        float ghostX, ghostY, ghostFacing;
        if (getTargetPlayerPosition(ghostX, ghostY, ghostFacing)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.90f, 1.0f));
            ImGui::Text("%s: (%.1f, %.1f)", L("playalong.target_position"),
                        ghostX, ghostY);
            ImGui::PopStyleColor();

            float dx = clone_.getX() - ghostX;
            float dy = clone_.getY() - ghostY;
            float distance = std::sqrt(dx * dx + dy * dy);

            ImVec4 distColor = distance < 5.0f ? ImVec4(0.40f, 0.80f, 0.40f, 1.0f) :
                               distance < 15.0f ? ImVec4(0.90f, 0.80f, 0.30f, 1.0f) :
                                                  ImVec4(0.90f, 0.40f, 0.40f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, distColor);
            ImGui::Text("%s: %.1f", L("playalong.distance"), distance);
            ImGui::PopStyleColor();

            // Trigger snap hint if too far and playing (once per encounter)
            if (distance > SNAP_HINT_DISTANCE && !snapHintShown_ && isPlaying_) {
                snapHintShown_ = true;
                snapHintTimeRemaining_ = SNAP_HINT_DURATION;
            }
        }

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
        ImGui::Text("WASD: %s | RMB: %s", L("playalong.move"), L("playalong.camera"));
        ImGui::Text("RShift: %s | ESC: %s", L("playalong.snap"), L("playalong.back"));
        ImGui::PopStyleColor();

        // Annotations toggle (only show if triggers are loaded)
        if (autoAnnotationTriggerManager_ && autoAnnotationTriggerManager_->hasTriggers()) {
            ImGui::Separator();
            ImGui::Checkbox(L("playalong.show_annotations"), &annotationsEnabled_);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void PlayAlongVisualizer::renderCountdownOverlay() {
    float scale = ImGui::GetIO().FontGlobalScale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Semi-transparent full-screen overlay
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##CountdownOverlay", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Calculate countdown number (ceil to show 3, 2, 1)
        int countdownNum = static_cast<int>(std::ceil(countdownTimeRemaining_));
        if (countdownNum < 1) countdownNum = 1;
        if (countdownNum > 3) countdownNum = 3;

        char numStr[8];
        snprintf(numStr, sizeof(numStr), "%d", countdownNum);

        // Large centered countdown number
        float fontSize = 120.0f * scale;
        ImVec2 textSize = ImGui::CalcTextSize(numStr);
        // Scale text size for large display
        textSize.x *= (fontSize / ImGui::GetFontSize());
        textSize.y *= (fontSize / ImGui::GetFontSize());

        ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                               viewport->Pos.y + viewport->Size.y * 0.5f);

        float circleRadius = 80.0f * scale;
        drawList->AddCircleFilled(center, circleRadius, IM_COL32(30, 30, 50, 200), 48);
        drawList->AddCircle(center, circleRadius, IM_COL32(100, 150, 255, 200), 48, 3.0f * scale);

        // Draw the number (approximate centering)
        ImVec2 textPos = ImVec2(center.x - 20.0f * scale, center.y - 40.0f * scale);
        ImGui::SetCursorScreenPos(textPos);
        ImGui::SetWindowFontScale(fontSize / ImGui::GetFontSize());
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", numStr);
        ImGui::SetWindowFontScale(1.0f);

        const char* startingText = L("playalong.countdown_starting");
        ImVec2 startingSize = ImGui::CalcTextSize(startingText);
        ImVec2 startingPos = ImVec2(center.x - startingSize.x * 0.5f, center.y + circleRadius + 20.0f * scale);
        drawList->AddText(startingPos, IM_COL32(200, 200, 220, 255), startingText);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void PlayAlongVisualizer::renderSnapHint() {
    float scale = ImGui::GetIO().FontGlobalScale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Toast notification at top center of screen
    float panelWidth = 500.0f * scale;
    float panelHeight = 50.0f * scale;

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + (viewport->Size.x - panelWidth) * 0.5f,
               viewport->Pos.y + 80.0f * scale));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));

    // Fade out during last second
    float alpha = snapHintTimeRemaining_ > 1.0f ? 0.95f : snapHintTimeRemaining_ * 0.95f;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.25f, alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f * scale, 12.0f * scale));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##SnapHint", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, alpha));
        ImGui::TextWrapped("%s", L("playalong.snap_hint"));
        ImGui::PopStyleColor();
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace PlayAlong
