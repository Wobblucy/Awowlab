#include "UI/Panels/Navigation/UIUpcomingBossCasts.h"
#include "UI/BossTimerSettings.h"
#include "UI/SpellIconLoader.h"
#include "UI/RaidNoteIconMenu.h"
#include "Database/SpellCastDatabase.h"
#include "Color/ActorColorGenerator.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

UIUpcomingBossCasts::UIUpcomingBossCasts() = default;
UIUpcomingBossCasts::~UIUpcomingBossCasts() = default;

void UIUpcomingBossCasts::setDependencies(
    SpellIconLoader* iconLoader,
    const SpellCastDatabase* spellCastDb,
    BossTimerSettings* settings,
    const ActorColorGenerator* colorGenerator
) {
    iconLoader_ = iconLoader;
    spellCastDb_ = spellCastDb;
    settings_ = settings;
    colorGenerator_ = colorGenerator;
    cacheDirty_ = true;
}

bool UIUpcomingBossCasts::isEnabled() const {
    return settings_ && settings_->upcomingCastsPanelEnabled;
}

float UIUpcomingBossCasts::getWidth() const {
    if (!settings_ || !settings_->upcomingCastsPanelEnabled) return 0.0f;
    return settings_->upcomingPanelWidth;
}

void UIUpcomingBossCasts::rebuildCache() {
    allHostileCasts_.clear();

    if (!spellCastDb_ || !colorGenerator_ || !settings_) {
        cacheDirty_ = false;
        return;
    }

    const auto& actorFlags = colorGenerator_->getActorFlags();
    const auto& allCasts = spellCastDb_->getAllActorCasts();

    // Collect all hostile casts that match enabled spells
    for (const auto& [guid, casts] : allCasts) {
        auto flagIt = actorFlags.find(std::string_view(guid));
        if (flagIt == actorFlags.end()) continue;
        if (!flagIt->second.isHostile() && !flagIt->second.isNeutral()) continue;

        std::string_view actorName = colorGenerator_->getActorName(guid);
        std::string sourceName = actorName.empty() ? guid : std::string(actorName);

        for (const auto& cast : casts) {
            // Skip melee (spell ID 0 and 1)
            if (cast.spell_id <= 1) continue;
            // Only include casts for spells that are enabled in settings
            if (!settings_->showCasts) continue;
            if (!settings_->isSpellEnabled(cast.spell_id)) continue;

            UpcomingCast upcoming;
            upcoming.spell_id = cast.spell_id;
            upcoming.timestamp_ms = cast.end_timestamp_ms;
            upcoming.source_name = sourceName;
            allHostileCasts_.push_back(std::move(upcoming));
        }
    }

    // Sort by timestamp ascending (earliest first)
    std::sort(allHostileCasts_.begin(), allHostileCasts_.end(),
        [](const UpcomingCast& a, const UpcomingCast& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    cacheDirty_ = false;
}

size_t UIUpcomingBossCasts::findFirstUpcomingCast(int32_t currentTimeMs) const {
    // Binary search for first cast with timestamp >= currentTime
    auto it = std::lower_bound(allHostileCasts_.begin(), allHostileCasts_.end(), currentTimeMs,
        [](const UpcomingCast& cast, int32_t time) {
            return cast.timestamp_ms < time;
        });
    return static_cast<size_t>(it - allHostileCasts_.begin());
}

void UIUpcomingBossCasts::render(int32_t currentTimeMs) {
    if (!settings_ || !settings_->upcomingCastsPanelEnabled) return;
    if (!iconLoader_ || !spellCastDb_ || !colorGenerator_) return;

    // Rebuild cache if dirty
    if (cacheDirty_) {
        rebuildCache();
    }

    // Bar configuration
    const float SCROLLING_WINDOW_SECONDS = 10.0f;  // Fixed 10s scrolling window
    const float LOOKAHEAD_SECONDS = settings_->upcomingWindowSeconds;  // How far ahead to show queued events
    const float ICON_SIZE = settings_->upcomingIconSize;
    const float ICON_SPACING = ICON_SIZE + 18.0f;  // Space for icon + countdown text below

    // Colors (matching GCD history style)
    const ImU32 BG_COLOR = IM_COL32(20, 20, 25, 200);
    const ImU32 BORDER_COLOR = IM_COL32(70, 130, 180, 255);      // Steel blue border
    const ImU32 QUEUED_COLOR = IM_COL32(100, 100, 120, 255);     // Dimmer for queued events
    const ImU32 IMMINENT_COLOR = IM_COL32(220, 60, 60, 255);     // Red for < 3s
    const ImU32 SOON_COLOR = IM_COL32(220, 180, 60, 255);        // Yellow for < 5s

    // Get available content area from parent window (set by UIWindowManager)
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float barWidth = contentSize.x;
    float barHeight = contentSize.y;

    if (barWidth < 20.0f || barHeight < 50.0f) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 basePos = ImGui::GetCursorScreenPos();

    drawList->AddRectFilled(
        basePos,
        ImVec2(basePos.x + barWidth, basePos.y + barHeight),
        BG_COLOR,
        4.0f
    );

    // Calculate layout for scrolling zone (bottom portion) and queued zone (top portion)
    float queuedZoneHeight = ICON_SPACING * 3;  // Space for ~3 queued icons at top
    float scrollingZoneTop = basePos.y + queuedZoneHeight;
    float scrollingZoneHeight = barHeight - queuedZoneHeight;

    float pixelsPerSecond = scrollingZoneHeight / SCROLLING_WINDOW_SECONDS;

    // Helper: convert timestamp to Y position for scrolling zone (0-10s)
    auto timeToY = [&](int32_t timestamp) -> float {
        float timeUntil = static_cast<float>(timestamp - currentTimeMs) / 1000.0f;
        // timeUntil=0 -> bottom of bar, timeUntil=10s -> top of scrolling zone
        return basePos.y + barHeight - (timeUntil * pixelsPerSecond) - ICON_SIZE;
    };

    size_t startIdx = findFirstUpcomingCast(currentTimeMs);
    int32_t scrollingEndMs = currentTimeMs + static_cast<int32_t>(SCROLLING_WINDOW_SECONDS * 1000.0f);
    int32_t lookaheadEndMs = currentTimeMs + static_cast<int32_t>(LOOKAHEAD_SECONDS * 1000.0f);

    // Collect queued events (beyond 10s but within lookahead)
    std::vector<size_t> queuedIndices;
    std::vector<size_t> scrollingIndices;

    for (size_t i = startIdx; i < allHostileCasts_.size(); ++i) {
        const auto& cast = allHostileCasts_[i];
        if (cast.timestamp_ms > lookaheadEndMs) break;

        if (cast.timestamp_ms > scrollingEndMs) {
            queuedIndices.push_back(i);
        } else {
            scrollingIndices.push_back(i);
        }
    }

    // Render queued events at top (offset vertically)
    // Earlier casts go at bottom (closer to scrolling zone), later casts at top
    size_t maxQueuedSlots = static_cast<size_t>((queuedZoneHeight - 8) / ICON_SPACING);
    size_t numToRender = std::min(queuedIndices.size(), maxQueuedSlots);

    for (size_t i = 0; i < numToRender; ++i) {
        // Render in reverse order: earliest cast at bottom, latest at top
        size_t idx = queuedIndices[numToRender - 1 - i];
        const auto& cast = allHostileCasts_[idx];
        int32_t countdownMs = cast.timestamp_ms - currentTimeMs;
        float countdownSec = static_cast<float>(countdownMs) / 1000.0f;
        float alpha = 0.6f;  // Dimmer for queued events

        float iconX = basePos.x + 4;
        float iconY = basePos.y + 4 + (i * ICON_SPACING);

        drawList->AddRectFilled(
            ImVec2(iconX - 2, iconY - 2),
            ImVec2(iconX + ICON_SIZE + 2, iconY + ICON_SIZE + 2),
            IM_COL32(0, 0, 0, static_cast<int>(180 * alpha)),
            4.0f
        );
        drawList->AddRect(
            ImVec2(iconX - 2, iconY - 2),
            ImVec2(iconX + ICON_SIZE + 2, iconY + ICON_SIZE + 2),
            QUEUED_COLOR,
            4.0f, 0, 1.0f
        );

        if (iconLoader_) {
            ImTextureID iconTex = iconLoader_->getSpellIcon(cast.spell_id);
            if (iconTex) {
                drawList->AddImage(
                    iconTex,
                    ImVec2(iconX, iconY),
                    ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE),
                    ImVec2(0, 0), ImVec2(1, 1),
                    IM_COL32(255, 255, 255, static_cast<int>(255 * alpha))
                );
            }
        }

        char countdownStr[16];
        snprintf(countdownStr, sizeof(countdownStr), "%.0f", countdownSec);
        ImVec2 countdownSize = ImGui::CalcTextSize(countdownStr);
        float countdownX = iconX + (ICON_SIZE - countdownSize.x) * 0.5f;
        float countdownY = iconY + ICON_SIZE + 2;
        drawList->AddText(ImVec2(countdownX + 1, countdownY + 1), IM_COL32(0, 0, 0, 200), countdownStr);
        drawList->AddText(ImVec2(countdownX, countdownY), IM_COL32(180, 180, 180, 255), countdownStr);

        const char* spellName = SPELL_NAME(cast.spell_id);
        float nameX = iconX + ICON_SIZE + 6;
        float nameY = iconY + (ICON_SIZE - ImGui::CalcTextSize(spellName).y) * 0.5f;
        float maxNameWidth = barWidth - (ICON_SIZE + 12);
        std::string displayName = spellName;
        if (ImGui::CalcTextSize(displayName.c_str()).x > maxNameWidth && maxNameWidth > 20) {
            while (displayName.length() > 3 && ImGui::CalcTextSize(displayName.c_str()).x > maxNameWidth - 10) {
                displayName.pop_back();
            }
            displayName += "...";
        }
        drawList->AddText(ImVec2(nameX + 1, nameY + 1), IM_COL32(0, 0, 0, static_cast<int>(150 * alpha)), displayName.c_str());
        drawList->AddText(ImVec2(nameX, nameY), IM_COL32(180, 180, 180, static_cast<int>(255 * alpha)), displayName.c_str());

        // Tooltip on hover
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= iconX - 2 && mousePos.x <= iconX + ICON_SIZE + 2 &&
            mousePos.y >= iconY - 2 && mousePos.y <= iconY + ICON_SIZE + 2) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", spellName);
            ImGui::TextDisabled("Source: %s", cast.source_name.c_str());
            ImGui::TextDisabled("In: %.0f seconds", countdownSec);
            ImGui::EndTooltip();
            SpellIconLoader::checkWowheadLookupRect(cast.spell_id,
                ImVec2(iconX, iconY), ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE));

            // Right-click to add to Raid Notes
            ui::checkRaidNoteContextMenuRect(
                cast.spell_id,
                spellName,
                cast.timestamp_ms,
                true,  // Boss casts are hostile
                ImVec2(iconX, iconY), ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE));
        }

    }

    // Draw separator line between queued and scrolling zones
    if (!queuedIndices.empty()) {
        float separatorY = scrollingZoneTop - 2;
        drawList->AddLine(
            ImVec2(basePos.x + 4, separatorY),
            ImVec2(basePos.x + barWidth - 4, separatorY),
            IM_COL32(100, 100, 120, 150),
            1.0f
        );
    }

    // Render scrolling events (within 10s)
    for (size_t idx : scrollingIndices) {
        const auto& cast = allHostileCasts_[idx];
        float iconY = timeToY(cast.timestamp_ms);

        // Skip if icon would be off-screen
        if (iconY < scrollingZoneTop - ICON_SIZE || iconY > basePos.y + barHeight) continue;

        int32_t countdownMs = cast.timestamp_ms - currentTimeMs;
        float countdownSec = static_cast<float>(countdownMs) / 1000.0f;

        // Calculate alpha based on proximity (closer = more opaque)
        float alpha = 0.5f + 0.5f * (1.0f - countdownSec / SCROLLING_WINDOW_SECONDS);
        alpha = std::clamp(alpha, 0.4f, 1.0f);

        // Determine border color based on urgency
        ImU32 borderCol = BORDER_COLOR;
        if (countdownMs < 3000) {
            // Pulse effect for imminent casts
            float pulse = 0.7f + 0.3f * sinf(static_cast<float>(ImGui::GetTime()) * 8.0f);
            borderCol = IM_COL32(
                static_cast<int>(220 * pulse),
                static_cast<int>(60 * pulse),
                static_cast<int>(60 * pulse),
                255
            );
        } else if (countdownMs < 5000) {
            borderCol = SOON_COLOR;
        }

        // Position icon on left side of bar (with small margin)
        float iconX = basePos.x + 4;

        drawList->AddRectFilled(
            ImVec2(iconX - 2, iconY - 2),
            ImVec2(iconX + ICON_SIZE + 2, iconY + ICON_SIZE + 2),
            IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)),
            4.0f
        );
        drawList->AddRect(
            ImVec2(iconX - 2, iconY - 2),
            ImVec2(iconX + ICON_SIZE + 2, iconY + ICON_SIZE + 2),
            borderCol,
            4.0f,
            0,
            2.0f
        );

        bool hasIcon = false;
        if (iconLoader_) {
            ImTextureID iconTex = iconLoader_->getSpellIcon(cast.spell_id);
            if (iconTex) {
                drawList->AddImage(
                    iconTex,
                    ImVec2(iconX, iconY),
                    ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE),
                    ImVec2(0, 0), ImVec2(1, 1),
                    IM_COL32(255, 255, 255, static_cast<int>(255 * alpha))
                );
                hasIcon = true;
            }
        }

        // Fallback placeholder
        if (!hasIcon) {
            drawList->AddRectFilled(
                ImVec2(iconX, iconY),
                ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE),
                IM_COL32(60, 60, 80, static_cast<int>(255 * alpha)),
                2.0f
            );
        }

        char countdownStr[16];
        if (countdownMs < 10000) {
            snprintf(countdownStr, sizeof(countdownStr), "%.1f", countdownSec);
        } else {
            snprintf(countdownStr, sizeof(countdownStr), "%.0f", countdownSec);
        }

        ImVec2 countdownSize = ImGui::CalcTextSize(countdownStr);
        float countdownX = iconX + (ICON_SIZE - countdownSize.x) * 0.5f;
        float countdownY = iconY + ICON_SIZE + 2;  // Below the icon

        // Countdown text color
        ImU32 textColor = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
        if (countdownMs < 3000) {
            textColor = IMMINENT_COLOR;
        } else if (countdownMs < 5000) {
            textColor = SOON_COLOR;
        }

        // Countdown with shadow (centered below icon)
        drawList->AddText(ImVec2(countdownX + 1, countdownY + 1), IM_COL32(0, 0, 0, 220), countdownStr);
        drawList->AddText(ImVec2(countdownX, countdownY), textColor, countdownStr);

        const char* spellName = SPELL_NAME(cast.spell_id);
        float nameX = iconX + ICON_SIZE + 6;
        float nameY = iconY + (ICON_SIZE - ImGui::CalcTextSize(spellName).y) * 0.5f;

        // Truncate name if too long for panel width
        float maxNameWidth = barWidth - (ICON_SIZE + 12);
        std::string displayName = spellName;
        if (ImGui::CalcTextSize(displayName.c_str()).x > maxNameWidth && maxNameWidth > 20) {
            while (displayName.length() > 3 && ImGui::CalcTextSize(displayName.c_str()).x > maxNameWidth - 10) {
                displayName.pop_back();
            }
            displayName += "...";
        }

        // Name text with shadow for readability
        drawList->AddText(ImVec2(nameX + 1, nameY + 1), IM_COL32(0, 0, 0, static_cast<int>(180 * alpha)), displayName.c_str());
        drawList->AddText(ImVec2(nameX, nameY), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), displayName.c_str());

        // Tooltip and Wowhead lookup on hover
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= iconX - 2 && mousePos.x <= iconX + ICON_SIZE + 2 &&
            mousePos.y >= iconY - 2 && mousePos.y <= iconY + ICON_SIZE + 2) {

            ImGui::BeginTooltip();
            ImGui::Text("%s", spellName);  // spellName already retrieved above
            ImGui::TextDisabled("Source: %s", cast.source_name.c_str());
            ImGui::TextDisabled("In: %.1f seconds", countdownSec);
            ImGui::EndTooltip();

            SpellIconLoader::checkWowheadLookupRect(cast.spell_id,
                ImVec2(iconX, iconY), ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE));

            // Right-click to add to Raid Notes
            ui::checkRaidNoteContextMenuRect(
                cast.spell_id,
                spellName,
                cast.timestamp_ms,
                true,  // Boss casts are hostile
                ImVec2(iconX, iconY), ImVec2(iconX + ICON_SIZE, iconY + ICON_SIZE));
        }
    }

    // Draw "NOW" marker at bottom
    float nowY = basePos.y + barHeight - 2;
    drawList->AddLine(
        ImVec2(basePos.x + 4, nowY),
        ImVec2(basePos.x + barWidth - 4, nowY),
        IM_COL32(255, 255, 255, 150),
        2.0f
    );

    // Reserve the space we used
    ImGui::Dummy(contentSize);
}
