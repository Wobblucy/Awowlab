#include "UI/Panels/Navigation/UIPlaybackPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/CameraSettings.h"
#include "UI/BossTimerSettings.h"
#include "Playback/PlaybackController.h"
#include "Camera/Camera3D.h"
#include "Core/LocalizationManager.h"
#include "../../Database/DeathDatabase.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

// Color palette for the playback bar
namespace PlaybackColors {
    // Background colors
    constexpr ImU32 PanelBg = IM_COL32(22, 22, 28, 255);
    constexpr ImU32 TimelineBg = IM_COL32(40, 40, 50, 255);
    constexpr ImU32 TimelineTrack = IM_COL32(55, 55, 70, 255);

    // Progress colors (gradient)
    constexpr ImU32 ProgressStart = IM_COL32(66, 135, 245, 255);   // Blue
    constexpr ImU32 ProgressEnd = IM_COL32(138, 180, 255, 255);    // Light blue

    // Playhead
    constexpr ImU32 Playhead = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 PlayheadGlow = IM_COL32(66, 135, 245, 100);

    // Death markers
    constexpr ImU32 DeathMarker = IM_COL32(220, 60, 60, 255);
    constexpr ImU32 DeathMarkerGlow = IM_COL32(220, 60, 60, 80);

    // Buttons
    constexpr ImU32 ButtonNormal = IM_COL32(70, 70, 85, 255);
    constexpr ImU32 ButtonHovered = IM_COL32(90, 90, 110, 255);
    constexpr ImU32 ButtonActive = IM_COL32(66, 135, 245, 255);
    constexpr ImU32 ButtonIcon = IM_COL32(220, 220, 230, 255);
    constexpr ImU32 ButtonIconActive = IM_COL32(255, 255, 255, 255);

    // Speed buttons
    constexpr ImU32 SpeedNormal = IM_COL32(50, 50, 65, 255);
    constexpr ImU32 SpeedSelected = IM_COL32(66, 135, 245, 255);
    constexpr ImU32 SpeedText = IM_COL32(180, 180, 190, 255);
    constexpr ImU32 SpeedTextSelected = IM_COL32(255, 255, 255, 255);

    // Time display
    constexpr ImU32 TimeText = IM_COL32(200, 200, 210, 255);
    constexpr ImU32 TimeSeparator = IM_COL32(100, 100, 120, 255);

    // Hover preview
    constexpr ImU32 HoverLine = IM_COL32(255, 255, 255, 120);
    constexpr ImU32 HoverTimeBg = IM_COL32(30, 30, 40, 230);
    constexpr ImU32 HoverTimeText = IM_COL32(255, 255, 255, 255);
}

void UIPlaybackPanel::formatTime(int32_t ms, char* buffer, size_t bufferSize) {
    // Handle negative timestamps (pre-encounter events)
    bool negative = ms < 0;
    uint32_t absMs = negative ? static_cast<uint32_t>(-ms) : static_cast<uint32_t>(ms);
    uint32_t totalSeconds = absMs / 1000;
    uint32_t minutes = totalSeconds / 60;
    uint32_t seconds = totalSeconds % 60;

    if (negative) {
        snprintf(buffer, bufferSize, "-%u:%02u", minutes, seconds);
    } else {
        snprintf(buffer, bufferSize, "%u:%02u", minutes, seconds);
    }
}

void UIPlaybackPanel::render(const std::shared_ptr<PlaybackController>& playback,
                             const DeathDatabase* deathDb,
                             CameraSettings& cameraSettings,
                             BossTimerSettings* bossTimerSettings) {
    if (!playback) return;

    // The caller has already opened the window via applyWindowState().

    float availWidth = ImGui::GetContentRegionAvail().x;

    // Row 1: Transport controls + Time display + Follow checkbox + Boss Timer checkbox + Speed controls
    ImGui::BeginGroup();
    {
        // Transport controls (left side)
        renderTransportControls(playback);

        ImGui::SameLine(0, 20);

        // Time display
        renderTimeDisplay(playback);

        ImGui::SameLine(0, 20);

        // Camera follow checkbox (compact, always visible)
        awlui::Checkbox(L("filter.follow"), &cameraSettings.enableFollow);

        // Boss Timer toggle (beside Follow)
        if (bossTimerSettings) {
            ImGui::SameLine(0, 15);
            awlui::Checkbox(L("playback.boss_timer"), &bossTimerSettings->enabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("playback.boss_timer_tooltip"));
            }
        }

        // Orthographic (top-down) toggle
        ImGui::SameLine(0, 15);
        awlui::Checkbox(L("playback.ortho"), &cameraSettings.orthographicMode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("playback.ortho_tooltip"));
        }

        // Speed controls (right side) - calculate position to right-align
        float speedWidth = 200.0f;  // Approximate width of speed controls
        float currentX = ImGui::GetCursorPosX();
        float targetX = availWidth - speedWidth;
        if (targetX > currentX) {
            ImGui::SameLine(targetX);
        } else {
            ImGui::SameLine();
        }
        renderSpeedControls(playback);
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    // Row 2: Timeline scrubber (full width)
    renderTimeline(playback, deathDb);
}

void UIPlaybackPanel::renderTransportControls(const std::shared_ptr<PlaybackController>& playback) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float buttonSize = 32.0f;
    const float smallButtonSize = 26.0f;
    const float spacing = 4.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Skip back 5 seconds button
    {
        ImVec2 btnMin = cursor;
        ImVec2 btnMax(cursor.x + smallButtonSize, cursor.y + smallButtonSize);

        ImGui::InvisibleButton("##SkipBack", ImVec2(smallButtonSize, smallButtonSize));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImU32 bgColor = hovered ? PlaybackColors::ButtonHovered : PlaybackColors::ButtonNormal;
        drawList->AddRectFilled(btnMin, btnMax, bgColor, 6.0f);

        // Draw rewind icon (<<) - two triangles pointing left
        float cx = (btnMin.x + btnMax.x) * 0.5f;
        float cy = (btnMin.y + btnMax.y) * 0.5f;
        float iconScale = 5.0f;

        // First triangle (right side)
        ImVec2 tri1[3] = {
            ImVec2(cx + iconScale, cy - iconScale),
            ImVec2(cx + iconScale, cy + iconScale),
            ImVec2(cx, cy)
        };
        drawList->AddTriangleFilled(tri1[0], tri1[1], tri1[2], PlaybackColors::ButtonIcon);

        // Second triangle (left side)
        ImVec2 tri2[3] = {
            ImVec2(cx, cy - iconScale),
            ImVec2(cx, cy + iconScale),
            ImVec2(cx - iconScale, cy)
        };
        drawList->AddTriangleFilled(tri2[0], tri2[1], tri2[2], PlaybackColors::ButtonIcon);

        if (hovered) {
            ImGui::SetTooltip("%s", L("playback.tooltip.skip_back"));
        }

        if (clicked) {
            int32_t currentTime = playback->getCurrentTime();
            int32_t minTime = playback->getMinTime();
            int32_t skipAmount = 5000;  // 5 seconds in ms
            int32_t newTime = (currentTime - skipAmount > minTime) ? currentTime - skipAmount : minTime;
            playback->setCurrentTime(newTime);
        }
    }

    cursor.x += smallButtonSize + spacing;
    ImGui::SetCursorScreenPos(cursor);

    // Play/Pause button (larger, centered)
    {
        ImVec2 btnMin = cursor;
        ImVec2 btnMax(cursor.x + buttonSize, cursor.y + buttonSize);

        float yOffset = (buttonSize - smallButtonSize) * 0.5f;
        btnMin.y -= yOffset;
        btnMax.y -= yOffset;

        ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y - yOffset));
        ImGui::InvisibleButton("##PlayPause", ImVec2(buttonSize, buttonSize));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        bool isPlaying = playback->isPlaying();
        ImU32 bgColor = isPlaying ? PlaybackColors::ButtonActive :
                       (hovered ? PlaybackColors::ButtonHovered : PlaybackColors::ButtonNormal);

        drawList->AddRectFilled(btnMin, btnMax, bgColor, 8.0f);

        float cx = (btnMin.x + btnMax.x) * 0.5f;
        float cy = (btnMin.y + btnMax.y) * 0.5f;
        float iconScale = 7.0f;

        ImU32 iconColor = isPlaying ? PlaybackColors::ButtonIconActive : PlaybackColors::ButtonIcon;

        if (isPlaying) {
            // Pause icon (two vertical bars)
            float barWidth = 3.0f;
            float barGap = 3.0f;
            drawList->AddRectFilled(
                ImVec2(cx - barGap - barWidth, cy - iconScale),
                ImVec2(cx - barGap, cy + iconScale),
                iconColor, 1.0f
            );
            drawList->AddRectFilled(
                ImVec2(cx + barGap, cy - iconScale),
                ImVec2(cx + barGap + barWidth, cy + iconScale),
                iconColor, 1.0f
            );
        } else {
            // Play icon (triangle pointing right)
            ImVec2 tri[3] = {
                ImVec2(cx - iconScale * 0.6f, cy - iconScale),
                ImVec2(cx - iconScale * 0.6f, cy + iconScale),
                ImVec2(cx + iconScale * 0.8f, cy)
            };
            drawList->AddTriangleFilled(tri[0], tri[1], tri[2], iconColor);
        }

        if (hovered) {
            ImGui::SetTooltip("%s", L("playback.tooltip.play_pause"));
        }

        if (clicked) {
            playback->togglePlayPause();
        }

        cursor.y += yOffset;  // Reset for next button
    }

    cursor.x += buttonSize + spacing;
    ImGui::SetCursorScreenPos(cursor);

    // Skip forward 5 seconds button
    {
        ImVec2 btnMin = cursor;
        ImVec2 btnMax(cursor.x + smallButtonSize, cursor.y + smallButtonSize);

        ImGui::InvisibleButton("##SkipForward", ImVec2(smallButtonSize, smallButtonSize));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImU32 bgColor = hovered ? PlaybackColors::ButtonHovered : PlaybackColors::ButtonNormal;
        drawList->AddRectFilled(btnMin, btnMax, bgColor, 6.0f);

        // Draw fast-forward icon (>>) - two triangles pointing right
        float cx = (btnMin.x + btnMax.x) * 0.5f;
        float cy = (btnMin.y + btnMax.y) * 0.5f;
        float iconScale = 5.0f;

        // First triangle (left side)
        ImVec2 tri1[3] = {
            ImVec2(cx - iconScale, cy - iconScale),
            ImVec2(cx - iconScale, cy + iconScale),
            ImVec2(cx, cy)
        };
        drawList->AddTriangleFilled(tri1[0], tri1[1], tri1[2], PlaybackColors::ButtonIcon);

        // Second triangle (right side)
        ImVec2 tri2[3] = {
            ImVec2(cx, cy - iconScale),
            ImVec2(cx, cy + iconScale),
            ImVec2(cx + iconScale, cy)
        };
        drawList->AddTriangleFilled(tri2[0], tri2[1], tri2[2], PlaybackColors::ButtonIcon);

        if (hovered) {
            ImGui::SetTooltip("%s", L("playback.tooltip.skip_forward"));
        }

        if (clicked) {
            int32_t currentTime = playback->getCurrentTime();
            int32_t maxTime = playback->getMaxTime();
            int32_t skipAmount = 5000;  // 5 seconds in ms
            int32_t newTime = (currentTime + skipAmount < maxTime) ? currentTime + skipAmount : maxTime;
            playback->setCurrentTime(newTime);
        }
    }

    cursor.x += smallButtonSize;
    ImGui::SetCursorScreenPos(cursor);
}

void UIPlaybackPanel::renderTimeDisplay(const std::shared_ptr<PlaybackController>& playback) {
    char currentTime[16];
    char totalTime[16];

    // Show actual current time (can be negative for pre-encounter events)
    // and total duration (always positive)
    int32_t current = playback->getCurrentTime();
    int32_t total = playback->getMaxTime() - playback->getMinTime();

    formatTime(current, currentTime, sizeof(currentTime));
    formatTime(total, totalTime, sizeof(totalTime));

    // Style: current / total
    ImGui::PushStyleColor(ImGuiCol_Text, PlaybackColors::TimeText);

    ImGui::Text("%s", currentTime);
    ImGui::SameLine(0, 2);
    ImGui::PushStyleColor(ImGuiCol_Text, PlaybackColors::TimeSeparator);
    ImGui::Text("/");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 2);
    ImGui::Text("%s", totalTime);

    ImGui::PopStyleColor();
}

void UIPlaybackPanel::renderSpeedControls(const std::shared_ptr<PlaybackController>& playback) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float currentSpeed = playback->getSpeed();

    const float sliderWidth = 120.0f;
    const float sliderHeight = 22.0f;
    const float buttonWidth = 28.0f;
    const float labelWidth = 40.0f;
    const float spacing = 6.0f;

    ImVec2 startCursor = ImGui::GetCursorScreenPos();
    float totalWidth = sliderWidth + spacing + labelWidth + spacing + buttonWidth;

    // Speed slider (0.1x to 10x)
    {
        ImVec2 sliderMin = startCursor;
        ImVec2 sliderMax(startCursor.x + sliderWidth, startCursor.y + sliderHeight);

        float trackHeight = 6.0f;
        float trackY = sliderMin.y + (sliderHeight - trackHeight) * 0.5f;
        ImVec2 trackMin(sliderMin.x, trackY);
        ImVec2 trackMax(sliderMax.x, trackY + trackHeight);
        drawList->AddRectFilled(trackMin, trackMax, PlaybackColors::TimelineTrack, trackHeight * 0.5f);

        // Calculate slider position (logarithmic scale for better feel)
        // log10(0.1) = -1, log10(10) = 1, so range is -1 to 1
        float logSpeed = std::log10(currentSpeed);
        float normalizedPos = (logSpeed + 1.0f) / 2.0f;  // Map -1..1 to 0..1
        normalizedPos = std::clamp(normalizedPos, 0.0f, 1.0f);

        float handleX = sliderMin.x + normalizedPos * sliderWidth;
        float handleRadius = 8.0f;

        // Progress fill
        if (normalizedPos > 0.0f) {
            ImVec2 fillMax(handleX, trackMax.y);
            drawList->AddRectFilled(trackMin, fillMax, PlaybackColors::ProgressStart, trackHeight * 0.5f);
        }

        float handleY = sliderMin.y + sliderHeight * 0.5f;
        drawList->AddCircleFilled(ImVec2(handleX, handleY), handleRadius, PlaybackColors::Playhead);
        drawList->AddCircleFilled(ImVec2(handleX, handleY), handleRadius - 2.0f, PlaybackColors::ProgressEnd);

        // Invisible button for interaction
        ImGui::SetCursorScreenPos(sliderMin);
        ImGui::InvisibleButton("##SpeedSlider", ImVec2(sliderWidth, sliderHeight));
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        if (hovered && !held) {
            ImGui::SetTooltip("%s", L("playback.tooltip.speed"));
        }

        if (held) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mouseNorm = std::clamp((mousePos.x - sliderMin.x) / sliderWidth, 0.0f, 1.0f);
            // Convert back from normalized to log scale to speed
            float logVal = mouseNorm * 2.0f - 1.0f;  // Map 0..1 to -1..1
            float newSpeed = std::pow(10.0f, logVal);
            // Snap to nice values near common speeds
            const float snapTargets[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 10.0f };
            for (float snap : snapTargets) {
                if (std::abs(newSpeed - snap) < snap * 0.08f) {
                    newSpeed = snap;
                    break;
                }
            }
            playback->setSpeed(newSpeed);
        }
    }

    // Speed label (e.g., "1.0x")
    {
        float labelX = startCursor.x + sliderWidth + spacing;
        ImVec2 labelPos(labelX, startCursor.y);

        char speedLabel[16];
        if (currentSpeed >= 1.0f) {
            snprintf(speedLabel, sizeof(speedLabel), "%.1fx", currentSpeed);
        } else {
            snprintf(speedLabel, sizeof(speedLabel), "%.2fx", currentSpeed);
        }

        ImVec2 textSize = ImGui::CalcTextSize(speedLabel);
        float textY = startCursor.y + (sliderHeight - textSize.y) * 0.5f;
        drawList->AddText(ImVec2(labelX, textY), PlaybackColors::TimeText, speedLabel);
    }

    // 1x reset button
    {
        float btnX = startCursor.x + sliderWidth + spacing + labelWidth + spacing;
        ImVec2 btnMin(btnX, startCursor.y);
        ImVec2 btnMax(btnX + buttonWidth, startCursor.y + sliderHeight);

        ImGui::SetCursorScreenPos(btnMin);
        ImGui::InvisibleButton("##Speed1x", ImVec2(buttonWidth, sliderHeight));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();
        bool isSelected = (std::abs(currentSpeed - 1.0f) < 0.01f);

        ImU32 bgColor = isSelected ? PlaybackColors::SpeedSelected :
                       (hovered ? PlaybackColors::ButtonHovered : PlaybackColors::SpeedNormal);
        drawList->AddRectFilled(btnMin, btnMax, bgColor, 4.0f);

        const char* label = "1x";
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 textPos(
            btnMin.x + (buttonWidth - textSize.x) * 0.5f,
            btnMin.y + (sliderHeight - textSize.y) * 0.5f
        );
        ImU32 textColor = isSelected ? PlaybackColors::SpeedTextSelected : PlaybackColors::SpeedText;
        drawList->AddText(textPos, textColor, label);

        if (clicked) {
            playback->setSpeed(1.0f);
        }
    }

    // Move cursor past all speed controls
    ImGui::SetCursorScreenPos(ImVec2(
        startCursor.x + totalWidth,
        startCursor.y + sliderHeight
    ));
}

void UIPlaybackPanel::renderTimeline(const std::shared_ptr<PlaybackController>& playback,
                                     const DeathDatabase* deathDb) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    int32_t minTime = playback->getMinTime();
    int32_t maxTime = playback->getMaxTime();
    int32_t currentTime = playback->getCurrentTime();
    int32_t duration = maxTime - minTime;

    if (duration == 0) return;

    float progress = static_cast<float>(currentTime - minTime) / static_cast<float>(duration);

    if (deathDb && (minTime != lastMinTime_ || maxTime != lastMaxTime_)) {
        lastMinTime_ = minTime;
        lastMaxTime_ = maxTime;
        deathPositions_.clear();

        auto playerDeaths = deathDb->getPlayerDeaths();
        for (const auto* death : playerDeaths) {
            if (death->timestamp_ms >= minTime && death->timestamp_ms <= maxTime) {
                float pos = static_cast<float>(death->timestamp_ms - minTime) / static_cast<float>(duration);
                deathPositions_.push_back(pos);
            }
        }
    }

    // Timeline dimensions
    const float timelineHeight = 20.0f;
    const float trackHeight = 6.0f;
    const float playheadRadius = 8.0f;

    float availWidth = ImGui::GetContentRegionAvail().x;
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImVec2 timelineMin = cursor;
    ImVec2 timelineMax(cursor.x + availWidth, cursor.y + timelineHeight);

    // Track position (centered vertically in timeline area)
    float trackY = cursor.y + (timelineHeight - trackHeight) * 0.5f;
    ImVec2 trackMin(cursor.x + playheadRadius, trackY);
    ImVec2 trackMax(cursor.x + availWidth - playheadRadius, trackY + trackHeight);
    float trackWidth = trackMax.x - trackMin.x;

    // Invisible button for interaction
    ImGui::InvisibleButton("##Timeline", ImVec2(availWidth, timelineHeight));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    ImVec2 mousePos = ImGui::GetMousePos();
    float mouseProgress = 0.0f;
    if (mousePos.x >= trackMin.x && mousePos.x <= trackMax.x) {
        mouseProgress = (mousePos.x - trackMin.x) / trackWidth;
    } else if (mousePos.x < trackMin.x) {
        mouseProgress = 0.0f;
    } else {
        mouseProgress = 1.0f;
    }

    if (held) {
        int32_t newTime = minTime + static_cast<int32_t>(mouseProgress * static_cast<float>(duration));
        playback->setCurrentTime(newTime);
        progress = mouseProgress;
    }

    drawList->AddRectFilled(trackMin, trackMax, PlaybackColors::TimelineTrack, trackHeight * 0.5f);

    if (progress > 0.0f) {
        float progressX = trackMin.x + progress * trackWidth;
        ImVec2 fillMax(progressX, trackMax.y);

        // Gradient from left to right
        drawList->AddRectFilledMultiColor(
            trackMin, fillMax,
            PlaybackColors::ProgressStart, PlaybackColors::ProgressEnd,
            PlaybackColors::ProgressEnd, PlaybackColors::ProgressStart
        );
    }

    for (float deathPos : deathPositions_) {
        float markerX = trackMin.x + deathPos * trackWidth;
        float markerY = trackY + trackHeight * 0.5f;

        // Glow
        drawList->AddCircleFilled(ImVec2(markerX, markerY), 6.0f, PlaybackColors::DeathMarkerGlow);
        // Marker
        drawList->AddCircleFilled(ImVec2(markerX, markerY), 3.0f, PlaybackColors::DeathMarker);

        // Skull-like indicator (small X or cross)
        float s = 2.0f;
        drawList->AddLine(
            ImVec2(markerX - s, markerY - s),
            ImVec2(markerX + s, markerY + s),
            IM_COL32(255, 255, 255, 200), 1.0f
        );
        drawList->AddLine(
            ImVec2(markerX + s, markerY - s),
            ImVec2(markerX - s, markerY + s),
            IM_COL32(255, 255, 255, 200), 1.0f
        );
    }

    float playheadX = trackMin.x + progress * trackWidth;
    float playheadY = trackY + trackHeight * 0.5f;

    // Playhead glow
    drawList->AddCircleFilled(ImVec2(playheadX, playheadY), playheadRadius + 2.0f, PlaybackColors::PlayheadGlow);
    // Playhead circle
    drawList->AddCircleFilled(ImVec2(playheadX, playheadY), playheadRadius, PlaybackColors::Playhead);
    // Inner circle for depth
    drawList->AddCircleFilled(ImVec2(playheadX, playheadY), playheadRadius - 2.0f, PlaybackColors::ProgressEnd);

    // Hover preview line and time tooltip
    if (hovered && !held) {
        float hoverX = std::clamp(mousePos.x, trackMin.x, trackMax.x);

        // Vertical line at hover position
        drawList->AddLine(
            ImVec2(hoverX, trackY - 4.0f),
            ImVec2(hoverX, trackY + trackHeight + 4.0f),
            PlaybackColors::HoverLine, 1.0f
        );

        // Time tooltip - calculate actual time (minTime + progress * duration)
        int32_t hoverTime = minTime + static_cast<int32_t>(mouseProgress * static_cast<float>(duration));
        char hoverTimeStr[16];
        formatTime(hoverTime, hoverTimeStr, sizeof(hoverTimeStr));

        ImVec2 textSize = ImGui::CalcTextSize(hoverTimeStr);
        float tooltipPadding = 4.0f;
        float tooltipWidth = textSize.x + tooltipPadding * 2;
        float tooltipHeight = textSize.y + tooltipPadding * 2;

        // Position tooltip above the hover line
        float tooltipX = hoverX - tooltipWidth * 0.5f;
        float tooltipY = trackY - tooltipHeight - 6.0f;

        // Clamp to stay in bounds
        tooltipX = std::clamp(tooltipX, timelineMin.x, timelineMax.x - tooltipWidth);

        ImVec2 tooltipMin(tooltipX, tooltipY);
        ImVec2 tooltipMax(tooltipX + tooltipWidth, tooltipY + tooltipHeight);

        drawList->AddRectFilled(tooltipMin, tooltipMax, PlaybackColors::HoverTimeBg, 3.0f);
        drawList->AddText(
            ImVec2(tooltipMin.x + tooltipPadding, tooltipMin.y + tooltipPadding),
            PlaybackColors::HoverTimeText,
            hoverTimeStr
        );
    }

    const int numTicks = 5;
    for (int i = 0; i <= numTicks; ++i) {
        float tickProgress = static_cast<float>(i) / static_cast<float>(numTicks);
        float tickX = trackMin.x + tickProgress * trackWidth;

        // Small tick mark
        drawList->AddLine(
            ImVec2(tickX, trackMax.y + 2.0f),
            ImVec2(tickX, trackMax.y + 5.0f),
            IM_COL32(80, 80, 100, 255), 1.0f
        );
    }
}

