#include "UI/Panels/Frames/UIEnemyGridPanel.h"
#include "Color/ActorColorGenerator.h"
#include "Animation/ActorPositionInterpolator.h"
#include "UI/Panels/Frames/UnitFrameBars.h"
#include "Core/LocalizationManager.h"
#include "Core/UIUtils.h"
#include "../../Database/OutputWriter.h"  // For ActorType enum
#include <imgui.h>
#include <algorithm>

void UIEnemyGridPanel::render(
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
    const std::unordered_map<std::string_view, std::string_view>& guidToName)
{
    // Clear any previous click state at the start of each frame
    clickedEnemy_ = std::nullopt;
    clearSelectionRequested_ = false;

    auto enemies = collectEnemies(colorGen, actorPositions, actorResources, guidToName);

    if (enemies.empty()) {
        ImGui::TextDisabled("%s", L("status.no_enemies"));
        return;
    }

    // Get available content region size and starting position
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    ImVec2 contentStart = ImGui::GetCursorPos();  // This is where content region starts

    // Small padding around the grid edges
    const float edgePadding = 4.0f;

    const int numEnemies = static_cast<int>(enemies.size());

    // Fixed 5 rows per column layout (like WoW raid groups)
    const int rowsPerColumn = 5;
    const float minCellWidth = 70.0f;
    const float maxCellWidth = 120.0f;
    const float minCellHeight = 35.0f;

    // Calculate number of columns needed (ceiling division by 5)
    const int cols = (numEnemies + rowsPerColumn - 1) / rowsPerColumn;

    // Actual rows is either 5 or fewer if we have less than 5 enemies
    const int rows = std::min(rowsPerColumn, numEnemies);

    // Calculate cell dimensions to fill available space
    // Width: edgePadding + cols*cellWidth + (cols-1)*cellPadding + edgePadding = availableSize.x
    float cellWidth = (availableSize.x - 2 * edgePadding - (cols - 1) * config_.cellPadding) / cols;
    cellWidth = std::clamp(cellWidth, minCellWidth, maxCellWidth);

    // Height: cells grow to fill vertical space for the 5-row layout
    float cellHeight = (availableSize.y - 2 * edgePadding - (rows - 1) * config_.cellPadding) / rows;
    cellHeight = std::max(cellHeight, minCellHeight);

    // Dynamically adjust bar heights based on cell height
    // Use 3.2:1 ratio for health:power bars
    float totalBarSpace = cellHeight - 8.0f;
    float healthBarHeight = std::max(12.0f, totalBarSpace * (3.2f / 4.2f));
    float powerBarHeight = std::max(4.0f, totalBarSpace * (1.0f / 4.2f));

    // Render grid manually with positioning (top-to-bottom in 5-row columns, then left-to-right)
    for (int i = 0; i < numEnemies; ++i) {
        int col = i / rowsPerColumn;  // Which column (group of 5)
        int row = i % rowsPerColumn;  // Position within the column (0-4)

        // Position from content region start with edge padding, then add cell+padding offsets
        float xPos = contentStart.x + edgePadding + col * (cellWidth + config_.cellPadding);
        float yPos = contentStart.y + edgePadding + row * (cellHeight + config_.cellPadding);

        ImGui::SetCursorPos(ImVec2(xPos, yPos));

        if (renderEnemyCell(enemies[i], cellWidth, cellHeight, healthBarHeight, powerBarHeight)) {
            clickedEnemy_ = enemies[i].guid;
        }
    }
}

std::vector<EnemyGridEntry> UIEnemyGridPanel::collectEnemies(
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
    const std::unordered_map<std::string_view, std::string_view>& guidToName)
{
    bool needsRebuild = (guidToName.size() != lastGuidToNameSize_);
    lastGuidToNameSize_ = guidToName.size();

    if (needsRebuild) {
        cachedEnemyOrder_.clear();

        struct EnemyHealthPair {
            std::string guid;
            uint64_t maxHealth;
        };
        std::vector<EnemyHealthPair> sortTemp;

        for (const auto& [guid, name] : guidToName) {
            const ActorType* typePtr = colorGen->getActorType(guid);
            if (!typePtr) {
                continue;
            }

            if (*typePtr != ActorType::Creature && *typePtr != ActorType::Vehicle) {
                continue;
            }

            const UnitFlags* flagsPtr = colorGen->getActorFlags(guid);
            if (flagsPtr) {
                if (flagsPtr->isFriendly()) {
                    continue;
                }
            }

            uint64_t maxHealth = 0;
            auto resIt = actorResources.find(guid);
            if (resIt != actorResources.end() && resIt->second.hasResource) {
                maxHealth = resIt->second.maxHealth;
            }

            sortTemp.push_back({std::string(guid), maxHealth});
        }

        std::sort(sortTemp.begin(), sortTemp.end(),
                  [](const EnemyHealthPair& a, const EnemyHealthPair& b) {
                      return a.maxHealth > b.maxHealth;
                  });

        cachedEnemyOrder_.reserve(sortTemp.size());
        for (const auto& e : sortTemp) {
            cachedEnemyOrder_.push_back(e.guid);
        }
    }

    enemiesBuffer_.clear();

    for (const auto& guid : cachedEnemyOrder_) {
        auto posIt = actorPositions.find(guid);
        bool isActive = (posIt != actorPositions.end() && posIt->second.hasPosition);

        if (!isActive) {
            continue;
        }

        auto nameIt = guidToName.find(guid);
        if (nameIt == guidToName.end()) continue;

        EnemyGridEntry enemy;
        enemy.guid = guid;
        enemy.name = std::string(nameIt->second);
        enemy.isActive = isActive;

        ActorColor color = colorGen->getCachedColor(guid);
        enemy.r = color.r;
        enemy.g = color.g;
        enemy.b = color.b;

        auto resIt = actorResources.find(guid);
        if (resIt != actorResources.end() && resIt->second.hasResource) {
            enemy.healthPercent = resIt->second.healthPercent;
            enemy.currentHealth = resIt->second.currentHealth;
            enemy.maxHealth = resIt->second.maxHealth;
            enemy.powerPercent = resIt->second.powerPercent;
            enemy.currentPower = resIt->second.currentPower;
            enemy.maxPower = resIt->second.maxPower;
            enemy.powerType = resIt->second.powerType;
            enemy.isDead = (resIt->second.currentHealth == 0 && resIt->second.maxHealth > 0);
        } else {
            enemy.healthPercent = 1.0f;
            enemy.currentHealth = 0;
            enemy.maxHealth = 0;
            enemy.powerPercent = 0.0f;
            enemy.currentPower = 0;
            enemy.maxPower = 0;
            enemy.powerType = -1;
            enemy.isDead = false;
        }

        enemiesBuffer_.push_back(enemy);
    }

    return enemiesBuffer_;
}

bool UIEnemyGridPanel::renderEnemyCell(const EnemyGridEntry& enemy,
                                        float cellWidth, float cellHeight,
                                        float healthBarHeight, float powerBarHeight)
{
    bool clicked = false;

    ImGui::PushID(enemy.guid.c_str());

    // Create a child region for the cell - use ChildBg style for background
    // Use more top padding (4.0f) to ensure content isn't clipped by border
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::BeginChild("Cell", ImVec2(cellWidth, cellHeight), true,
                      ImGuiWindowFlags_NoScrollbar);

    // Fade inactive enemies
    float alpha = enemy.isActive ? 1.0f : 0.4f;

    float barWidth = ImGui::GetContentRegionAvail().x;

    // Remember position before health bar for text overlay
    ImVec2 healthBarPos = ImGui::GetCursorPos();

    // Health bar with enemy color
    unit_frame::drawHealthBar(enemy.healthPercent, enemy.r, enemy.g, enemy.b,
                              barWidth, healthBarHeight, enemy.isDead);

    // Enemy name: first 5 characters, centered on the health bar (UTF-8 safe)
    std::string displayName = ui::utf8Truncate(enemy.name, 5);

    ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
    float textX = (barWidth - textSize.x) * 0.5f;
    if (textX < 0) textX = 0;

    float textY = (healthBarHeight - textSize.y) * 0.5f;
    if (textY < 0) textY = 0;

    // Draw drop shadow (offset down-right)
    ImGui::SetCursorPos(ImVec2(healthBarPos.x + textX + 1.0f, healthBarPos.y + textY + 1.0f));
    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.7f * alpha), "%s", displayName.c_str());

    ImGui::SetCursorPos(ImVec2(healthBarPos.x + textX, healthBarPos.y + textY));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", displayName.c_str());

    // Only render power bar if we actually have power data
    bool hasPowerData = (enemy.maxPower > 0 && enemy.powerType >= 0);
    if (hasPowerData) {
        // Move cursor to after health bar for power bar
        ImGui::SetCursorPos(ImVec2(healthBarPos.x, healthBarPos.y + healthBarHeight));
        unit_frame::drawPowerBar(enemy.powerPercent, enemy.powerType, barWidth, powerBarHeight);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Check if the cell was clicked (left click to select)
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        clicked = true;
    }

    // Right-click context menu for clearing selection
    if (ImGui::BeginPopupContextItem("EnemyCellContext")) {
        if (ImGui::MenuItem(L("btn.clear_selection"))) {
            clearSelectionRequested_ = true;
        }
        ImGui::EndPopup();
    }

    // Tooltip with detailed info on hover
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(enemy.r, enemy.g, enemy.b, 1.0f), "%s", enemy.name.c_str());

        if (enemy.maxHealth > 0) {
            ImGui::Text(L("format.health_full"),
                        static_cast<unsigned long long>(enemy.currentHealth),
                        static_cast<unsigned long long>(enemy.maxHealth),
                        enemy.healthPercent * 100.0f);
        }

        if (enemy.maxPower > 0) {
            const char* powerName = L("power.power");
            switch (enemy.powerType) {
                case 0: powerName = L("power.mana"); break;
                case 1: powerName = L("power.rage"); break;
                case 2: powerName = L("power.focus"); break;
                case 3: powerName = L("power.energy"); break;
                case 6: powerName = L("power.runic_power"); break;
                case 12: powerName = L("power.chi"); break;
            }
            ImGui::Text(L("format.power_full"),
                        powerName, enemy.currentPower, enemy.maxPower,
                        enemy.powerPercent * 100.0f);
        }

        if (enemy.isDead) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", L("status.dead"));
        }

        ImGui::EndTooltip();
    }

    ImGui::PopID();

    return clicked;
}
