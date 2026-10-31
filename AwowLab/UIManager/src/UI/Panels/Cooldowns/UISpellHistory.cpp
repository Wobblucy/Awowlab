#include "UI/Panels/Cooldowns/UISpellHistory.h"
#include "UI/SpellIconLoader.h"
#include "UI/RaidNoteIconMenu.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>

bool UISpellHistory::render(const std::optional<std::string>& selectedActorGuid,
                            const std::vector<SpellCastDisplay>& recentCasts,
                            int32_t currentTime,
                            SpellIconLoader* iconLoader,
                            bool asWindow) {
    if (asWindow) {
        ImGui::Begin(L("panel.gcd_history"));
    }

    if (!selectedActorGuid.has_value()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("gcd.no_player"));
        ImGui::Text("%s", L("gcd.select_hint"));

        if (asWindow) {
            ImGui::End();
        }
        return false;
    }

    renderSpellBar(recentCasts, currentTime, iconLoader, selectedActorGuid.value());

    if (asWindow) {
        ImGui::End();
    }
    return true;
}

void UISpellHistory::renderSpellBar(const std::vector<SpellCastDisplay>& recentCasts,
                                     int32_t currentTime,
                                     SpellIconLoader* iconLoader,
                                     const std::string& actorGuid) {
    // WeakAura-style GCD bar configuration
    constexpr float TIME_WINDOW_SECONDS = 8.0f;  // Show ~8 seconds of history

    // Colors
    const ImU32 GREY_BAR_COLOR = IM_COL32(120, 120, 120, 200);    // Grey while casting
    const ImU32 BLUE_BAR_COLOR = IM_COL32(40, 100, 180, 200);     // Blue (completed cast)
    const ImU32 RED_BAR_COLOR = IM_COL32(180, 40, 40, 200);       // Red for cancelled
    const ImU32 OFF_GCD_COLOR = IM_COL32(180, 80, 40, 200);       // Orange for off-GCD icon fallback
    const ImU32 BORDER_COLOR = IM_COL32(200, 200, 50, 255);       // Gold border
    const ImU32 BG_COLOR = IM_COL32(20, 20, 25, 180);             // Dark background
    const ImU32 CANCELLED_BORDER = IM_COL32(220, 60, 60, 255);    // Red border for cancelled

    // Separate casts into on-GCD and off-GCD
    std::vector<const SpellCastDisplay*> onGcdCasts;
    std::vector<const SpellCastDisplay*> offGcdCasts;

    for (const auto& cast : recentCasts) {
        // Use start_timestamp for filtering since icon is at start position now
        int32_t displayTs = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;
        float timeSinceCast = static_cast<float>(currentTime - displayTs) / 1000.0f;
        if (timeSinceCast >= 0 && timeSinceCast < TIME_WINDOW_SECONDS + 2.0f) {
            if (cast.gcd_ms == 0) {
                offGcdCasts.push_back(&cast);
            } else {
                onGcdCasts.push_back(&cast);
            }
        }
    }

    // Create a child window that fills available space
    ImGui::BeginChild("GCDScrollArea", ImVec2(0, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Get actual dimensions from child window's content region
    const float BAR_WIDTH = ImGui::GetContentRegionAvail().x;
    const float BAR_HEIGHT = ImGui::GetContentRegionAvail().y;
    const float PIXELS_PER_SECOND = BAR_WIDTH / TIME_WINDOW_SECONDS;

    // Calculate row height to fill available space (2 rows)
    const float ROW_HEIGHT = BAR_HEIGHT / 2.0f;
    const float ICON_SIZE_SCALED = std::min(ROW_HEIGHT - 4.0f, 48.0f);  // Cap icon size

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 basePos = ImGui::GetCursorScreenPos();

    drawList->AddRectFilled(basePos,
                           ImVec2(basePos.x + BAR_WIDTH, basePos.y + BAR_HEIGHT),
                           BG_COLOR, 4.0f);

    // Helper: convert timestamp to X position (right edge = now, scrolls left)
    auto timeToX = [&](int32_t timestamp) -> float {
        float timeSince = static_cast<float>(currentTime - timestamp) / 1000.0f;
        return basePos.x + BAR_WIDTH - (timeSince * PIXELS_PER_SECOND);
    };

    // Layout:
    // - Icon at the start position (when the ability triggered)
    // - Cast-time spells: icon, grey bar while casting, then blue (success) or red (failed)
    // - Instant spells: icon only, no GCD bar
    // - Off-GCD: icon only, on the bottom row
    auto drawSpellCast = [&](const SpellCastDisplay* cast, float yOffset, bool isOffGcd) {
        float yPos = basePos.y + yOffset + 2.0f;
        float barY = yPos + ICON_SIZE_SCALED * 0.15f;
        float barH = ICON_SIZE_SCALED * 0.7f;

        // Icon position: right edge at the start timestamp (when the ability triggered)
        // This way the icon is visible immediately when cast and scrolls left
        int32_t iconTimestamp = cast->start_timestamp_ms > 0 ? cast->start_timestamp_ms : cast->end_timestamp_ms;
        float rawIconX = timeToX(iconTimestamp) - ICON_SIZE_SCALED;

        // Ensure there's always room for at least a small GCD bar to be visible
        // Cap icon position so it doesn't go beyond (right edge - icon size - min bar width)
        constexpr float MIN_BAR_WIDTH = 4.0f;  // Minimum visible bar width
        float maxIconX = basePos.x + BAR_WIDTH - ICON_SIZE_SCALED - MIN_BAR_WIDTH;
        float iconX = std::min(rawIconX, maxIconX);

        // Calculate alpha based on age (fade out older casts)
        float timeSinceStart = static_cast<float>(currentTime - iconTimestamp) / 1000.0f;
        float alpha = 1.0f - (timeSinceStart / TIME_WINDOW_SECONDS) * 0.5f;
        alpha = std::max(0.3f, std::min(1.0f, alpha));

        // Draw cast bar for cast-time spells and cancelled casts only
        // (No GCD bar for instant spells - removed due to accuracy issues)
        const bool isCancelled = cast->cancelled;
        bool isActuallyCastTime = cast->has_cast_time() ||
            (cast->start_timestamp_ms > 0 && cast->start_timestamp_ms < cast->end_timestamp_ms);

        if (!isOffGcd && (isCancelled || isActuallyCastTime)) {
            float barStartX = timeToX(iconTimestamp);
            float barEndX;
            ImU32 barColor;

            if (isCancelled) {
                // Cancelled cast: grey while "casting", then red once the failure is seen
                int32_t cancelDisplayEnd = cast->end_timestamp_ms;
                if (cast->expected_cast_time_ms > 0) {
                    int32_t expectedEnd = cast->start_timestamp_ms + cast->expected_cast_time_ms;
                    cancelDisplayEnd = std::min(cancelDisplayEnd, expectedEnd);
                }

                if (currentTime < cancelDisplayEnd) {
                    barEndX = timeToX(currentTime);
                    barColor = GREY_BAR_COLOR;
                } else {
                    barEndX = timeToX(cancelDisplayEnd);
                    barColor = RED_BAR_COLOR;
                }
            } else {
                // Cast-time spell (not cancelled)
                bool stillCasting = currentTime < cast->end_timestamp_ms;

                if (stillCasting) {
                    barEndX = timeToX(currentTime);
                    barColor = GREY_BAR_COLOR;
                } else {
                    barEndX = timeToX(cast->end_timestamp_ms);
                    barColor = BLUE_BAR_COLOR;
                }
            }

            // Clip to visible area
            float visibleStart = std::max(barStartX, basePos.x);
            float visibleEnd = std::min(barEndX, basePos.x + BAR_WIDTH);

            if (visibleStart < visibleEnd) {
                barColor = (barColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);
                drawList->AddRectFilled(
                    ImVec2(visibleStart, barY),
                    ImVec2(visibleEnd, barY + barH),
                    barColor, 2.0f);
            }
        }

        // Skip icon if completely off-screen
        if (iconX + ICON_SIZE_SCALED < basePos.x || iconX > basePos.x + BAR_WIDTH) return;

        // Clip icon to visible area
        float visibleStart = std::max(iconX, basePos.x);
        float visibleEnd = std::min(iconX + ICON_SIZE_SCALED, basePos.x + BAR_WIDTH);
        if (visibleStart >= visibleEnd) return;

        ImTextureID iconTexture = 0;
        if (iconLoader) {
            iconTexture = iconLoader->getSpellIcon(cast->spell_id);
        }

        // Icon bounds
        ImVec2 iconMin(iconX, yPos);
        ImVec2 iconMax(iconX + ICON_SIZE_SCALED, yPos + ICON_SIZE_SCALED);

        // Draw icon (only the visible portion)
        if (iconTexture != 0) {
            float uvMinX = (visibleStart - iconX) / ICON_SIZE_SCALED;
            float uvMaxX = (visibleEnd - iconX) / ICON_SIZE_SCALED;

            // Red tint for cancelled casts, white for successful
            ImVec4 tintColor = cast->cancelled
                ? ImVec4(1.0f, 0.5f, 0.5f, alpha)   // Red-tinted for cancelled
                : ImVec4(1.0f, 1.0f, 1.0f, alpha);  // Normal
            drawList->AddImage(iconTexture,
                              ImVec2(visibleStart, yPos),
                              ImVec2(visibleEnd, yPos + ICON_SIZE_SCALED),
                              ImVec2(uvMinX, 0), ImVec2(uvMaxX, 1),
                              ImGui::GetColorU32(tintColor));
        } else {
            // Fallback: draw colored rect (red for cancelled)
            ImU32 fillColor = cast->cancelled ? RED_BAR_COLOR
                            : (isOffGcd ? OFF_GCD_COLOR : BLUE_BAR_COLOR);
            fillColor = (fillColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);
            drawList->AddRectFilled(
                ImVec2(visibleStart, yPos),
                ImVec2(visibleEnd, yPos + ICON_SIZE_SCALED),
                fillColor, 2.0f);
        }

        // Draw border (red for cancelled, gold for successful)
        if (iconX >= basePos.x - ICON_SIZE_SCALED && iconX < basePos.x + BAR_WIDTH) {
            ImU32 borderBase = cast->cancelled ? CANCELLED_BORDER : BORDER_COLOR;
            ImU32 borderWithAlpha = (borderBase & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);
            drawList->AddRect(iconMin, iconMax, borderWithAlpha, 2.0f, 0, 2.0f);

            SpellIconLoader::checkWowheadLookupRect(cast->spell_id, iconMin, iconMax);

            ui::checkRaidNoteContextMenuRect(
                cast->spell_id,
                SPELL_NAME(cast->spell_id),
                cast->end_timestamp_ms,
                false,  // Player casts are not hostile
                iconMin, iconMax,
                {},     // No grouped timestamps
                actorGuid);
        }
    };

    // Draw on-GCD spells (top row)
    for (const auto* cast : onGcdCasts) {
        drawSpellCast(cast, 0.0f, false);
    }

    // Draw off-GCD spells (bottom row)
    for (const auto* cast : offGcdCasts) {
        drawSpellCast(cast, ROW_HEIGHT, true);
    }

    // Draw encounter timeline with polished visual design
    float currentTimeSec = currentTime / 1000.0f;
    float leftTimeSec = currentTimeSec - TIME_WINDOW_SECONDS;

    int startSecond = std::max(0, static_cast<int>(std::floor(leftTimeSec)));
    int endSecond = static_cast<int>(std::ceil(currentTimeSec));

    // Timeline aesthetics - scales with window, ~18% of panel height
    const float TIMELINE_HEIGHT = std::max(18.0f, BAR_HEIGHT * 0.18f);
    const float timelineY = basePos.y + BAR_HEIGHT - TIMELINE_HEIGHT;

    // Draw subtle gradient backdrop for timeline area
    drawList->AddRectFilledMultiColor(
        ImVec2(basePos.x, timelineY),
        ImVec2(basePos.x + BAR_WIDTH, basePos.y + BAR_HEIGHT),
        IM_COL32(15, 15, 20, 0),      // Top: transparent
        IM_COL32(15, 15, 20, 0),
        IM_COL32(25, 25, 35, 200),    // Bottom: subtle dark
        IM_COL32(25, 25, 35, 200));

    for (int sec = startSecond; sec <= endSecond; ++sec) {
        float markerX = basePos.x + BAR_WIDTH - ((currentTimeSec - sec) * PIXELS_PER_SECOND);

        if (markerX < basePos.x || markerX > basePos.x + BAR_WIDTH) continue;

        bool isLegend = (sec % 5 == 0);

        if (isLegend) {
            // Major marker: gradient line from top to bottom with warm accent
            const ImU32 topColor = IM_COL32(200, 170, 100, 40);   // Subtle warm at top
            const ImU32 bottomColor = IM_COL32(220, 180, 120, 180); // Warm gold at bottom
            drawList->AddLine(
                ImVec2(markerX, basePos.y + 4),
                ImVec2(markerX, timelineY),
                topColor, 1.0f);
            drawList->AddLine(
                ImVec2(markerX, timelineY),
                ImVec2(markerX, basePos.y + BAR_HEIGHT - TIMELINE_HEIGHT * 0.1f),
                bottomColor, 1.5f);

            // Diamond marker at bottom - scales with timeline
            const float diamondSize = TIMELINE_HEIGHT * 0.2f;
            const float diamondY = basePos.y + BAR_HEIGHT - TIMELINE_HEIGHT * 0.25f;
            drawList->AddQuadFilled(
                ImVec2(markerX, diamondY - diamondSize),
                ImVec2(markerX + diamondSize, diamondY),
                ImVec2(markerX, diamondY + diamondSize),
                ImVec2(markerX - diamondSize, diamondY),
                IM_COL32(220, 190, 130, 220));

            // Time label with subtle glow effect
            char timeLabel[16];
            int mins = sec / 60;
            int secs = sec % 60;
            snprintf(timeLabel, sizeof(timeLabel), "%d:%02d", mins, secs);
            ImVec2 textSize = ImGui::CalcTextSize(timeLabel);
            float textX = markerX - textSize.x * 0.5f;
            float textY = timelineY + (TIMELINE_HEIGHT - textSize.y) * 0.35f;  // Centered in timeline

            // Draw shadow layers for glow effect (instead of ugly box)
            const ImU32 shadowColor = IM_COL32(0, 0, 0, 180);
            drawList->AddText(ImVec2(textX + 1, textY + 1), shadowColor, timeLabel);
            drawList->AddText(ImVec2(textX - 1, textY + 1), shadowColor, timeLabel);

            // Main text with warm highlight
            drawList->AddText(ImVec2(textX, textY), IM_COL32(240, 220, 170, 255), timeLabel);
        } else {
            // Minor marker: subtle tick in timeline area
            const float tickTop = timelineY + TIMELINE_HEIGHT * 0.15f;
            const float tickBottom = basePos.y + BAR_HEIGHT - TIMELINE_HEIGHT * 0.15f;
            drawList->AddLine(
                ImVec2(markerX, tickTop),
                ImVec2(markerX, tickBottom),
                IM_COL32(100, 100, 120, 60), 1.0f);

            // Small bright tick at bottom
            drawList->AddLine(
                ImVec2(markerX, tickBottom),
                ImVec2(markerX, basePos.y + BAR_HEIGHT - 1),
                IM_COL32(120, 120, 140, 120), 1.0f);
        }
    }

    drawList->AddLine(
        ImVec2(basePos.x, timelineY),
        ImVec2(basePos.x + BAR_WIDTH, timelineY),
        IM_COL32(80, 80, 100, 60), 1.0f);

    // Draw row labels (always show both)
    ImVec2 labelPos(basePos.x + 4, basePos.y + 2);
    drawList->AddText(labelPos, IM_COL32(200, 200, 200, 180), L("gcd.row_gcd"));
    labelPos.y += ROW_HEIGHT;
    drawList->AddText(labelPos, IM_COL32(200, 200, 200, 180), L("gcd.row_ogcd"));

    ImGui::EndChild();

#ifndef NDEBUG
    // Debug info in separate window - doesn't affect main panel layout
    if (iconLoader && !recentCasts.empty()) {
        ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Spell History Debug", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
            float debugTimeSec = currentTime / 1000.0f;
            ImGui::Text("On-GCD: %zu, Off-GCD: %zu | Time: %.1fs | Scale: %.1f px/s",
                       onGcdCasts.size(), offGcdCasts.size(), debugTimeSec, PIXELS_PER_SECOND);
            ImGui::Separator();

            size_t showCount = std::min(recentCasts.size(), size_t(8));
            for (size_t i = recentCasts.size() - showCount; i < recentCasts.size(); ++i) {
                const auto& cast = recentCasts[i];
                float timeSince = (currentTime - cast.end_timestamp_ms) / 1000.0f;
                const char* status = cast.cancelled ? " [CANCELLED]" : "";
                const char* castType = cast.has_cast_time() ? "Cast" : "Inst";

                ImGui::Text("ID:%u %s Haste:%.1f%% (%.1fs ago)%s",
                           cast.spell_id, castType,
                           cast.haste_percent(), timeSince, status);
            }
        }
        ImGui::End();
    }
#endif
}
