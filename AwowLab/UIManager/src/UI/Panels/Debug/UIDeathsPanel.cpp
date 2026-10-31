#include "UI/Panels/Debug/UIDeathsPanel.h"
#include "UI/UIActorSelector.h"
#include "UI/SpellIconLoader.h"
#include "../../Database/DeathDatabase.h"
#include "../../Database/CombatDatabase.h"
#include "../../Database/ResourceDatabase.h"
#include "Playback/PlaybackController.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>

UIDeathsPanel::UIDeathsPanel() = default;

void UIDeathsPanel::render(
    const DeathDatabase* deathDb,
    std::shared_ptr<PlaybackController> playback,
    UIActorSelector* actorSelector,
    const CombatDatabase* combatDb,
    const ResourceDatabase* resourceDb,
    const std::unordered_map<uint32_t, std::string>* spellNameMap,
    SpellIconLoader* iconLoader
) {
    // Cache dependencies for popup rendering
    combatDb_ = combatDb;
    resourceDb_ = resourceDb;
    spellNameMap_ = spellNameMap;
    iconLoader_ = iconLoader;

    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(L("panel.deaths"), &visible_)) {
        ImGui::End();
        return;
    }

    // The popup holds a copy of one death. If that death is no longer in the
    // database (another encounter loaded), its cached events are stale too.
    if (selectedDeath_) {
        bool stillPresent = false;
        if (deathDb) {
            for (const auto& death : deathDb->getAllDeaths()) {
                if (death.timestamp_ms == selectedDeath_->timestamp_ms &&
                    death.actor_guid == selectedDeath_->actor_guid) {
                    stillPresent = true;
                    break;
                }
            }
        }
        if (!stillPresent) {
            closeDeathBreakout();
        }
    }

    if (!deathDb || deathDb->getAllDeaths().empty()) {
        ImGui::TextDisabled("%s", L("status.no_deaths"));
        ImGui::End();
        return;
    }

    auto playerDeaths = deathDb->getPlayerDeaths();

    if (playerDeaths.empty()) {
        ImGui::TextDisabled("%s", L("status.no_player_deaths"));
        ImGui::End();
        return;
    }

    ImGui::Text(L("format.player_deaths"), playerDeaths.size());
    ImGui::Separator();

    // Table with columns: Time, Player, Killing Ability
    if (ImGui::BeginTable("DeathsTable", 3,
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn(L("deaths.col_time"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn(L("deaths.col_player"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("deaths.col_ability"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto* death : playerDeaths) {
            ImGui::TableNextRow();

            // Time column (clickable)
            ImGui::TableSetColumnIndex(0);
            std::string timeStr = ui::format::formatTimestamp(death->timestamp_ms);

            // Make the row selectable for clicking
            ImGui::PushID(death);
            if (ImGui::Selectable(timeStr.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                // Snap timeline to 4 seconds before death for slow-mo replay
                if (playback) {
                    int32_t rewindTime = (death->timestamp_ms > 4000)
                        ? death->timestamp_ms - 4000
                        : playback->getMinTime();
                    playback->setCurrentTime(rewindTime);
                    playback->setSpeed(0.5f);  // 50% speed for slow-mo
                    playback->play();          // Start playback
                }
                // Focus on the player who died
                if (actorSelector) {
                    actorSelector->setSelectedActor(death->actor_guid);
                }
            }

            // Right-click to open death analysis popup
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                openDeathBreakout(death);
            }

            // Tooltip hint
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("deaths.right_click_hint"));
            }

            ImGui::PopID();

            // Player name column
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(death->actor_name.c_str());

            // Killing ability column
            ImGui::TableSetColumnIndex(2);
            if (!death->killing_spell_name.empty()) {
                ImGui::TextUnformatted(death->killing_spell_name.c_str());
            } else if (combatDb && spellNameMap) {
                // Query damage events in the 2 seconds before death
                int32_t startTime = (death->timestamp_ms > 2000) ? death->timestamp_ms - 2000 : 0;
                auto events = combatDb->getEventsForTarget(
                    death->actor_guid, startTime, death->timestamp_ms, 50);

                uint32_t highestDamageSpellId = 0;
                int64_t highestDamage = 0;
                for (const auto& event : events) {
                    if (event.is_damage && event.amount > highestDamage) {
                        highestDamage = event.amount;
                        highestDamageSpellId = event.spell_id;
                    }
                }

                if (highestDamageSpellId > 0) {
                    if (iconLoader && highestDamageSpellId > 1) {
                        if (iconLoader->renderSpellIcon(highestDamageSpellId, ImVec2(16, 16))) {
                            ImGui::SameLine();
                        }
                    }
                    // Show spell name - use SpellNameDatabase for reliable lookup
                    if (highestDamageSpellId == 1) {
                        ImGui::TextUnformatted("Melee");
                    } else {
                        ImGui::TextUnformatted(SPELL_NAME(highestDamageSpellId));
                    }
                } else {
                    ImGui::TextDisabled("%s", L("deaths.unknown"));
                }
            } else {
                ImGui::TextDisabled("%s", L("deaths.unknown"));
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();

    // Render the popup window (if open)
    renderDeathBreakoutPopup();
}

void UIDeathsPanel::openDeathBreakout(const DeathEvent* death) {
    if (!death) return;

    selectedDeath_ = *death;
    showBreakoutPopup_ = true;

    // Query events for this death - full 30 seconds before death (no event limit)
    cachedEvents_.clear();
    if (combatDb_) {
        int32_t startTime = (death->timestamp_ms > 30000) ? death->timestamp_ms - 30000 : 0;
        cachedEvents_ = combatDb_->getEventsForTarget(
            death->actor_guid,
            startTime,
            death->timestamp_ms,
            0  // No limit - let the filter decide what to show
        );
    }

    // Query health history for this death
    cachedHealthHistory_.clear();
    cachedMaxHealth_ = 0;
    if (resourceDb_) {
        auto allResources = resourceDb_->getAllResourcesForActor(death->actor_guid);
        int32_t startTime = (death->timestamp_ms > 30000) ? death->timestamp_ms - 30000 : 0;

        // Filter to just the time window before death
        for (const auto& event : allResources) {
            if (event.timestamp_ms >= startTime && event.timestamp_ms <= death->timestamp_ms) {
                cachedHealthHistory_.push_back(event);
                if (event.max_health > cachedMaxHealth_) {
                    cachedMaxHealth_ = event.max_health;
                }
            }
        }
    }
}

void UIDeathsPanel::renderDeathBreakoutPopup() {
    if (!showBreakoutPopup_ || !selectedDeath_) return;

    ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);

    // Use fixed window ID so position/size persists across different players
    // Display player name in title but use "###" to set a fixed ID
    std::string title = selectedDeath_->actor_name + " - " + L("deaths.analysis_title") + "###DeathAnalysis";
    if (ImGui::Begin(title.c_str(), &showBreakoutPopup_)) {
        // Death info header
        ImGui::Text("%s: %s", L("deaths.player"), selectedDeath_->actor_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("@ %s", ui::format::formatTimestamp(selectedDeath_->timestamp_ms).c_str());

        if (!selectedDeath_->killing_spell_name.empty()) {
            ImGui::Text("%s: %s", L("deaths.killing_ability"), selectedDeath_->killing_spell_name.c_str());
        }

        // Event filter slider
        ImGui::Spacing();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat(L("deaths.filter_threshold"), &eventFilterThreshold_, 0.0f, 20.0f, "%.0f%%");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("deaths.filter_tooltip"));
        }

        ImGui::Separator();

        // Health chart (top section)
        ImGui::Text("%s", L("deaths.health_chart"));
        renderHealthChart(ImGui::GetContentRegionAvail().x, 120.0f);

        ImGui::Spacing();
        ImGui::Separator();

        // Event list (bottom section)
        renderEventList();
    }
    ImGui::End();

    if (!showBreakoutPopup_) {
        closeDeathBreakout();
    }
}

void UIDeathsPanel::closeDeathBreakout() {
    showBreakoutPopup_ = false;
    selectedDeath_.reset();
    cachedEvents_.clear();
    cachedHealthHistory_.clear();
}

void UIDeathsPanel::renderHealthChart(float width, float height) {
    if (cachedHealthHistory_.empty()) {
        ImGui::TextDisabled("%s", L("deaths.no_health_data"));
        ImGui::Dummy(ImVec2(width, height));
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    drawList->AddRectFilled(
        pos,
        ImVec2(pos.x + width, pos.y + height),
        IM_COL32(30, 30, 40, 255)
    );

    drawList->AddRect(
        pos,
        ImVec2(pos.x + width, pos.y + height),
        IM_COL32(60, 60, 80, 255)
    );

    // Calculate time window (30s before death)
    int32_t deathTime = selectedDeath_->timestamp_ms;
    int32_t windowDuration = 30000;
    int32_t startTime = (deathTime > windowDuration) ? deathTime - windowDuration : 0;
    float timeRange = static_cast<float>(deathTime - startTime);
    if (timeRange < 1.0f) timeRange = 1.0f;

    for (int i = 1; i < 4; ++i) {
        float y = pos.y + (height * i / 4.0f);
        drawList->AddLine(
            ImVec2(pos.x, y),
            ImVec2(pos.x + width, y),
            IM_COL32(50, 50, 70, 128)
        );
    }

    ImVec2 prevPoint;
    bool firstPoint = true;
    for (const auto& event : cachedHealthHistory_) {
        if (event.timestamp_ms < startTime || event.timestamp_ms > deathTime) continue;

        float healthPct = (event.max_health > 0)
            ? static_cast<float>(event.current_health) / static_cast<float>(event.max_health)
            : 0.0f;
        healthPct = std::clamp(healthPct, 0.0f, 1.0f);

        float x = pos.x + ((event.timestamp_ms - startTime) / timeRange) * width;
        float y = pos.y + height - (healthPct * height);

        ImVec2 point(x, y);
        if (!firstPoint) {
            // Color gradient: green (high health) to red (low health)
            uint8_t r = static_cast<uint8_t>((1.0f - healthPct) * 255);
            uint8_t g = static_cast<uint8_t>(healthPct * 200);
            drawList->AddLine(prevPoint, point, IM_COL32(r, g, 80, 255), 2.0f);
        }
        prevPoint = point;
        firstPoint = false;
    }

    // Draw final line to 0% health at death time (red)
    if (!firstPoint) {
        float deathX = pos.x + width;  // Death is at the right edge
        float deathY = pos.y + height;  // 0% health is at the bottom
        ImVec2 deathPoint(deathX, deathY);
        drawList->AddLine(prevPoint, deathPoint, IM_COL32(255, 0, 80, 255), 2.0f);
    }

    // Reserve space for the chart
    ImGui::Dummy(ImVec2(width, height));

    ImGui::TextDisabled("-30s                                              Death");
}

void UIDeathsPanel::renderEventList() {
    // Count visible events when filtering is active
    size_t visibleCount = cachedEvents_.size();
    bool filterActive = cachedMaxHealth_ > 0 && eventFilterThreshold_ > 0.0f;

    if (filterActive) {
        visibleCount = 0;
        for (const auto& event : cachedEvents_) {
            float eventPct = (static_cast<float>(event.amount) / static_cast<float>(cachedMaxHealth_)) * 100.0f;
            if (eventPct >= eventFilterThreshold_) {
                ++visibleCount;
            }
        }
        ImGui::Text("%s (%zu of %zu)", L("deaths.last_events"), visibleCount, cachedEvents_.size());
    } else {
        ImGui::Text("%s (%zu)", L("deaths.last_events"), cachedEvents_.size());
    }

    if (cachedEvents_.empty()) {
        ImGui::TextDisabled("%s", L("deaths.no_events"));
        return;
    }

    float availHeight = ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginTable("EventsTable", 3,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
        ImVec2(0, availHeight))) {

        ImGui::TableSetupColumn(L("deaths.col_time"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn(L("deaths.col_ability"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("deaths.col_amount"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (const auto& event : cachedEvents_) {
            // Filter out events below threshold percentage of max health
            if (filterActive) {
                float eventPct = (static_cast<float>(event.amount) / static_cast<float>(cachedMaxHealth_)) * 100.0f;
                if (eventPct < eventFilterThreshold_) {
                    continue;
                }
            }

            ImGui::TableNextRow();

            // Find health percentage at this event's timestamp
            // Look for the closest health snapshot at or before this event
            float healthPct = 1.0f;  // Default to full health
            for (auto it = cachedHealthHistory_.rbegin(); it != cachedHealthHistory_.rend(); ++it) {
                if (it->timestamp_ms <= event.timestamp_ms && it->max_health > 0) {
                    healthPct = static_cast<float>(it->current_health) / static_cast<float>(it->max_health);
                    healthPct = std::clamp(healthPct, 0.0f, 1.0f);
                    break;
                }
            }

            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 50, 50, 100));

            // Capture row start position for health bar overlay
            ImGui::TableSetColumnIndex(0);
            ImVec2 rowStartPos = ImGui::GetCursorScreenPos();

            // Time column
            std::string timeStr = ui::format::formatTimestamp(event.timestamp_ms);
            ImGui::TextUnformatted(timeStr.c_str());

            // Ability column (with icon if available)
            ImGui::TableSetColumnIndex(1);
            if (iconLoader_ && event.spell_id > 0) {
                if (iconLoader_->renderSpellIcon(event.spell_id, ImVec2(16, 16))) {
                    ImGui::SameLine();
                }
            }

            // Look up spell name - use SpellNameDatabase for reliable lookup
            if (event.spell_id == 0 || event.spell_id == 1) {
                ImGui::TextUnformatted("Melee");
            } else {
                ImGui::TextUnformatted(SPELL_NAME(event.spell_id));
            }

            // Crit indicator
            if (event.is_crit) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "*");
            }

            // Amount column
            ImGui::TableSetColumnIndex(2);
            std::string amountStr;
            if (event.is_damage) {
                amountStr = "-" + ui::format::formatAmount(event.amount);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", amountStr.c_str());
            } else {
                amountStr = "+" + ui::format::formatAmount(event.amount);
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", amountStr.c_str());
            }

            // Draw health bar overlay after all content is placed
            // Get row dimensions from table
            float rowHeight = ImGui::GetTextLineHeightWithSpacing();
            [[maybe_unused]] float tableWidth = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();

            // Use window's work rect for full table width
            ImVec2 windowPos = ImGui::GetWindowPos();
            float windowWidth = ImGui::GetWindowContentRegionMax().x;
            float rowWidth = windowWidth - (rowStartPos.x - windowPos.x) + 8.0f;  // Add small padding

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 barColor = event.is_damage ? IM_COL32(120, 50, 50, 150) : IM_COL32(50, 120, 50, 150);
            drawList->AddRectFilled(
                rowStartPos,
                ImVec2(rowStartPos.x + rowWidth * healthPct, rowStartPos.y + rowHeight),
                barColor
            );
        }

        ImGui::EndTable();
    }
}
