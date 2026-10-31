#include "PlayAlong/UIPlayAlongCombinedGCD.h"
#include "UI/SpellIconLoader.h"
#include "Database/SpellCastDatabase.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace PlayAlong {

void UIPlayAlongCombinedGCD::render(const std::string& targetPlayerGuid,
                                     int32_t currentTimeMs,
                                     const SpellCastDatabase* spellCastDb,
                                     SpellIconLoader* iconLoader,
                                     const PlayAlongFeedbackSystem* feedbackSystem) {
    const float TOTAL_WINDOW = PAST_WINDOW_SECONDS + FUTURE_WINDOW_SECONDS;

    const ImU32 PAST_BAR_COLOR = IM_COL32(60, 60, 80, 200);
    const ImU32 FUTURE_BAR_COLOR = IM_COL32(80, 180, 80, 200);
    const ImU32 FUTURE_NEXT_COLOR = IM_COL32(255, 200, 50, 255);
    const ImU32 GOOD_COLOR = IM_COL32(50, 200, 50, 255);
    const ImU32 BAD_COLOR = IM_COL32(220, 50, 50, 255);
    const ImU32 NEUTRAL_COLOR = IM_COL32(150, 150, 150, 200);
    const ImU32 BORDER_COLOR = IM_COL32(200, 200, 200, 200);
    const ImU32 MARKER_COLOR = IM_COL32(255, 255, 255, 255);
    const ImU32 PAST_ZONE_COLOR = IM_COL32(40, 40, 50, 100);
    const ImU32 FUTURE_ZONE_COLOR = IM_COL32(30, 50, 30, 100);
    const ImU32 SEPARATOR_COLOR = IM_COL32(80, 80, 100, 150);

    float scale = ImGui::GetIO().FontGlobalScale;

    float panelWidth = PANEL_WIDTH * scale;
    float panelHeight = (ROW_HEIGHT * 2.0f + 8.0f) * scale;  // Two rows + separator

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + (viewport->Size.x - panelWidth) * 0.5f,
               viewport->Pos.y + (viewport->Size.y - panelHeight) * 0.5f + 80.0f * scale),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(300.0f * scale, 80.0f * scale),
        ImVec2(1200.0f * scale, 300.0f * scale)
    );

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.22f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 8.0f * scale));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("##PlayAlongGCDTimeline", nullptr, flags)) {
        // Drop keyboard focus on click so WASD keeps moving the clone
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            ImGui::SetKeyboardFocusHere(-1);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 basePos = ImGui::GetCursorScreenPos();

        ImVec2 windowSize = ImGui::GetWindowSize();
        float actualWidth = windowSize.x;
        float actualHeight = windowSize.y;

        float barWidth = actualWidth - 16.0f * scale;
        // Two rows share the height left after padding (16) and the separator gap (8)
        float rowHeight = (actualHeight - 24.0f * scale) * 0.5f;
        float iconSize = std::min(rowHeight - 12.0f * scale, ICON_SIZE * scale);
        float centerX = basePos.x + (barWidth * PAST_WINDOW_SECONDS / TOTAL_WINDOW);

        auto timeToX = [&](int32_t timestamp) -> float {
            float secondsFromNow = (timestamp - currentTimeMs) / 1000.0f;
            float normalizedPos = (PAST_WINDOW_SECONDS + secondsFromNow) / TOTAL_WINDOW;
            return basePos.x + normalizedPos * barWidth;
        };

        ImVec2 inputRowStart = basePos;
        float inputRowHeight = rowHeight - 4.0f * scale;

        drawList->AddRectFilled(inputRowStart,
                               ImVec2(inputRowStart.x + barWidth, inputRowStart.y + inputRowHeight),
                               IM_COL32(40, 40, 50, 150), 4.0f);

        drawList->AddText(ImVec2(inputRowStart.x + 4, inputRowStart.y + 2),
                         IM_COL32(150, 150, 160, 200), L("playalong.your_inputs"));

        if (feedbackSystem) {
            uint32_t good = feedbackSystem->getTotalGood();
            uint32_t bad = feedbackSystem->getTotalBad();
            uint32_t missed = feedbackSystem->getTotalMissed();

            char scoreStr[64];
            snprintf(scoreStr, sizeof(scoreStr), "%u/%u (%.0f%%)",
                     good, good + bad + missed,
                     feedbackSystem->getOverallAccuracy() * 100.0f);

            ImVec2 scoreTextSize = ImGui::CalcTextSize(scoreStr);
            float scoreX = inputRowStart.x + barWidth - scoreTextSize.x - 8.0f;
            float scoreY = inputRowStart.y + 2.0f;

            float accuracy = feedbackSystem->getOverallAccuracy();
            ImU32 scoreColor;
            if (accuracy >= 0.9f) {
                scoreColor = IM_COL32(50, 255, 50, 255);
            } else if (accuracy >= 0.7f) {
                scoreColor = IM_COL32(255, 200, 50, 255);
            } else {
                scoreColor = IM_COL32(255, 100, 100, 255);
            }
            drawList->AddText(ImVec2(scoreX, scoreY), scoreColor, scoreStr);

            const auto& recentFeedback = feedbackSystem->getRecentFeedback();
            float iconY = inputRowStart.y + (inputRowHeight - iconSize) * 0.5f;
            int32_t pastWindowMs = static_cast<int32_t>(PAST_WINDOW_SECONDS * 1000);

            for (const auto& event : recentFeedback) {
                if (event.playerPressMs <= 0) continue;
                if (event.playerPressMs > currentTimeMs) continue;
                if (event.playerPressMs < currentTimeMs - pastWindowMs) continue;

                float iconX = timeToX(event.playerPressMs) - iconSize * 0.5f;
                if (iconX + iconSize < inputRowStart.x || iconX > centerX) continue;

                ImU32 resultColor;
                switch (event.result) {
                    case FeedbackResult::Good: resultColor = GOOD_COLOR; break;
                    case FeedbackResult::Bad: resultColor = BAD_COLOR; break;
                    default: resultColor = NEUTRAL_COLOR; break;
                }

                float secondsAgo = (currentTimeMs - event.playerPressMs) / 1000.0f;
                float alpha = 1.0f - (secondsAgo / PAST_WINDOW_SECONDS) * 0.5f;
                alpha = std::max(0.4f, std::min(1.0f, alpha));

                ImTextureID iconTexture = iconLoader ? iconLoader->getSpellIcon(event.spellId) : 0;

                if (iconTexture != 0) {
                    ImVec4 tintColor(1.0f, 1.0f, 1.0f, alpha);
                    drawList->AddImage(iconTexture,
                                      ImVec2(iconX, iconY),
                                      ImVec2(iconX + iconSize, iconY + iconSize),
                                      ImVec2(0, 0), ImVec2(1, 1),
                                      ImGui::GetColorU32(tintColor));
                } else {
                    ImU32 fillColor = (resultColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);
                    drawList->AddRectFilled(ImVec2(iconX, iconY),
                                           ImVec2(iconX + iconSize, iconY + iconSize),
                                           fillColor, 4.0f);
                }

                ImU32 borderAlpha = (resultColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);
                float borderThickness = (event.result == FeedbackResult::Good) ? 3.0f : 2.5f;
                drawList->AddRect(ImVec2(iconX, iconY),
                                 ImVec2(iconX + iconSize, iconY + iconSize),
                                 borderAlpha, 4.0f, 0, borderThickness);

                SpellIconLoader::checkWowheadLookupRect(event.spellId,
                    ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize));

                if (event.result == FeedbackResult::Good && event.deltaMs != 0) {
                    char deltaStr[16];
                    if (event.deltaMs < 0) {
                        snprintf(deltaStr, sizeof(deltaStr), "-%dms", -event.deltaMs);
                    } else {
                        snprintf(deltaStr, sizeof(deltaStr), "+%dms", event.deltaMs);
                    }

                    ImVec2 textSize = ImGui::CalcTextSize(deltaStr);
                    float textX = iconX + (iconSize - textSize.x) * 0.5f;
                    float textY = iconY + iconSize + 2.0f;

                    drawList->AddRectFilled(
                        ImVec2(textX - 2, textY - 1),
                        ImVec2(textX + textSize.x + 2, textY + textSize.y + 1),
                        IM_COL32(0, 0, 0, 180), 2.0f);

                    ImU32 textColor = (event.deltaMs < 0)
                        ? IM_COL32(100, 200, 255, 200)
                        : IM_COL32(255, 200, 100, 200);
                    drawList->AddText(ImVec2(textX, textY), textColor, deltaStr);
                }
            }
        }

        float separatorY = inputRowStart.y + inputRowHeight + 2.0f * scale;
        drawList->AddLine(ImVec2(basePos.x, separatorY),
                         ImVec2(basePos.x + barWidth, separatorY),
                         SEPARATOR_COLOR, 1.0f);

        ImVec2 expectedRowStart = ImVec2(basePos.x, separatorY + 4.0f * scale);
        float expectedRowHeight = rowHeight - 4.0f * scale;

        drawList->AddRectFilled(expectedRowStart,
                               ImVec2(centerX, expectedRowStart.y + expectedRowHeight),
                               PAST_ZONE_COLOR, 4.0f, ImDrawFlags_RoundCornersLeft);
        drawList->AddRectFilled(ImVec2(centerX, expectedRowStart.y),
                               ImVec2(expectedRowStart.x + barWidth, expectedRowStart.y + expectedRowHeight),
                               FUTURE_ZONE_COLOR, 4.0f, ImDrawFlags_RoundCornersRight);

        drawList->AddRect(expectedRowStart,
                         ImVec2(expectedRowStart.x + barWidth, expectedRowStart.y + expectedRowHeight),
                         BORDER_COLOR, 4.0f, 0, 1.0f);

        drawList->AddText(ImVec2(expectedRowStart.x + 4, expectedRowStart.y + 2),
                         IM_COL32(150, 150, 160, 200), L("playalong.past"));
        drawList->AddText(ImVec2(expectedRowStart.x + barWidth - 50 * scale, expectedRowStart.y + 2),
                         IM_COL32(150, 200, 150, 200), L("playalong.upcoming"));

        if (spellCastDb && !targetPlayerGuid.empty()) {
            const auto& allCasts = spellCastDb->getAllActorCasts();
            auto it = allCasts.find(targetPlayerGuid);
            if (it != allCasts.end()) {
                const auto& playerCasts = it->second;

                std::vector<SpellCastDisplay> pastCasts;
                std::vector<SpellCastDisplay> futureCasts;

                int32_t pastStartTime = currentTimeMs - static_cast<int32_t>(PAST_WINDOW_SECONDS * 1000);
                int32_t futureEndTime = currentTimeMs + static_cast<int32_t>(FUTURE_WINDOW_SECONDS * 1000);

                for (const auto& cast : playerCasts) {
                    if (cast.spell_id == 0 || cast.spell_id == 1) continue;  // melee
                    if (feedbackSystem && feedbackSystem->getKeybinds().isSpellIgnored(cast.spell_id)) continue;

                    int32_t castTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;
                    if (castTime < pastStartTime || castTime > futureEndTime) continue;

                    SpellCastDisplay display{
                        cast.spell_id,
                        cast.start_timestamp_ms,
                        cast.end_timestamp_ms,
                        1500,
                        static_cast<uint16_t>(cast.cast_duration_ms()),
                        static_cast<int16_t>(cast.haste_permille),
                        cast.cancelled
                    };

                    if (castTime <= currentTimeMs) {
                        pastCasts.push_back(display);
                    } else {
                        futureCasts.push_back(display);
                    }
                }

                std::sort(pastCasts.begin(), pastCasts.end(), [](const auto& a, const auto& b) {
                    return a.end_timestamp_ms < b.end_timestamp_ms;
                });
                std::sort(futureCasts.begin(), futureCasts.end(), [](const auto& a, const auto& b) {
                    return a.end_timestamp_ms < b.end_timestamp_ms;
                });

                float iconY = expectedRowStart.y + (expectedRowHeight - iconSize) * 0.5f;

                for (const auto& cast : pastCasts) {
                    int32_t castTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;
                    float iconX = timeToX(castTime) - iconSize * 0.5f;
                    if (iconX + iconSize < expectedRowStart.x || iconX > expectedRowStart.x + barWidth) continue;

                    float secondsAgo = (currentTimeMs - castTime) / 1000.0f;
                    float alpha = 1.0f - (secondsAgo / PAST_WINDOW_SECONDS) * 0.6f;
                    alpha = std::max(0.3f, std::min(1.0f, alpha));

                    ImTextureID iconTexture = iconLoader ? iconLoader->getSpellIcon(cast.spell_id) : 0;

                    if (iconTexture != 0) {
                        ImVec4 tintColor(0.6f, 0.6f, 0.7f, alpha);
                        drawList->AddImage(iconTexture,
                                          ImVec2(iconX, iconY),
                                          ImVec2(iconX + iconSize, iconY + iconSize),
                                          ImVec2(0, 0), ImVec2(1, 1),
                                          ImGui::GetColorU32(tintColor));
                    } else {
                        ImU32 fillColor = (PAST_BAR_COLOR & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);
                        drawList->AddRectFilled(ImVec2(iconX, iconY),
                                               ImVec2(iconX + iconSize, iconY + iconSize),
                                               fillColor, 4.0f);
                    }

                    ImU32 borderAlpha = (BORDER_COLOR & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 150) << 24);
                    drawList->AddRect(ImVec2(iconX, iconY),
                                     ImVec2(iconX + iconSize, iconY + iconSize),
                                     borderAlpha, 4.0f, 0, 1.0f);

                    SpellIconLoader::checkWowheadLookupRect(cast.spell_id,
                        ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize));
                }

                bool isFirstFuture = true;
                for (const auto& cast : futureCasts) {
                    int32_t castTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;
                    float iconX = timeToX(castTime) - iconSize * 0.5f;
                    if (iconX + iconSize < expectedRowStart.x || iconX > expectedRowStart.x + barWidth) continue;

                    float secondsUntil = (castTime - currentTimeMs) / 1000.0f;
                    float emphasis = 1.0f - (secondsUntil / FUTURE_WINDOW_SECONDS) * 0.3f;

                    ImTextureID iconTexture = iconLoader ? iconLoader->getSpellIcon(cast.spell_id) : 0;

                    ImU32 borderColor = isFirstFuture ? FUTURE_NEXT_COLOR : BORDER_COLOR;
                    float borderThickness = isFirstFuture ? 3.0f : 1.5f;

                    if (iconTexture != 0) {
                        ImVec4 tintColor = isFirstFuture
                            ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                            : ImVec4(0.9f, 1.0f, 0.9f, emphasis);
                        drawList->AddImage(iconTexture,
                                          ImVec2(iconX, iconY),
                                          ImVec2(iconX + iconSize, iconY + iconSize),
                                          ImVec2(0, 0), ImVec2(1, 1),
                                          ImGui::GetColorU32(tintColor));
                    } else {
                        ImU32 fillColor = isFirstFuture ? FUTURE_NEXT_COLOR : FUTURE_BAR_COLOR;
                        drawList->AddRectFilled(ImVec2(iconX, iconY),
                                               ImVec2(iconX + iconSize, iconY + iconSize),
                                               fillColor, 4.0f);
                    }

                    drawList->AddRect(ImVec2(iconX, iconY),
                                     ImVec2(iconX + iconSize, iconY + iconSize),
                                     borderColor, 4.0f, 0, borderThickness);

                    SpellIconLoader::checkWowheadLookupRect(cast.spell_id,
                        ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize));

                    isFirstFuture = false;
                }
            }
        }

        float markerTop = inputRowStart.y + 2;
        float markerBottom = expectedRowStart.y + expectedRowHeight - 2;

        drawList->AddLine(ImVec2(centerX, markerTop),
                         ImVec2(centerX, markerBottom),
                         MARKER_COLOR, 2.0f);

        drawList->AddTriangleFilled(
            ImVec2(centerX - 6, markerTop),
            ImVec2(centerX + 6, markerTop),
            ImVec2(centerX, markerTop + 8),
            MARKER_COLOR);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

} // namespace PlayAlong
