#include "UI/Panels/Frames/UIRaidFramePanel.h"
#include "UI/SpellIconLoader.h"
#include "UI/SpellAssignmentSettings.h"
#include "Color/ActorColorGenerator.h"
#include "Core/UIUtils.h"
#include "Animation/ActorPositionInterpolator.h"
#include "UI/Panels/Frames/UnitFrameBars.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellStateComputer.h"
#include "../../Database/OutputWriter.h"  // For ActorType enum
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/AuraDatabase.h"
#include <imgui.h>
#include <algorithm>
#include <unordered_set>

void UIRaidFramePanel::render(
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    SpellIconLoader* iconLoader,
    uint32_t currentTimeMs)
{
    // Clear any previous click state at the start of each frame
    clickedPlayer_ = std::nullopt;
    clearSelectionRequested_ = false;

    auto players = collectRaidPlayers(colorGen, actorPositions, actorResources, guidToName);

    if (players.empty()) {
        ImGui::TextDisabled("%s", L("status.no_friendly_players"));
        return;
    }

    // Get available content region size and starting position
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    ImVec2 contentStart = ImGui::GetCursorPos();  // This is where content region starts

    // Small padding around the grid edges
    const float edgePadding = 4.0f;

    const int numPlayers = static_cast<int>(players.size());

    // Fixed 5 rows per column layout (like WoW raid groups)
    const int rowsPerColumn = 5;
    const float minCellWidth = 70.0f;
    const float maxCellWidth = 120.0f;
    const float minCellHeight = 35.0f;

    // Calculate number of columns needed (ceiling division by 5)
    const int cols = (numPlayers + rowsPerColumn - 1) / rowsPerColumn;

    // Actual rows is either 5 or fewer if we have less than 5 players
    const int rows = std::min(rowsPerColumn, numPlayers);

    // Calculate cell dimensions to fill available space
    // Width: edgePadding + cols*cellWidth + (cols-1)*cellPadding + edgePadding = availableSize.x
    float cellWidth = (availableSize.x - 2 * edgePadding - (cols - 1) * config_.cellPadding) / cols;
    cellWidth = std::clamp(cellWidth, minCellWidth, maxCellWidth);

    // Height: cells grow to fill vertical space for the 5-row layout
    float cellHeight = (availableSize.y - 2 * edgePadding - (rows - 1) * config_.cellPadding) / rows;
    cellHeight = std::max(cellHeight, minCellHeight);

    // Dynamically adjust bar heights based on cell height
    // Use 3.2:1 ratio for health:power bars
    // Total bar space = cellHeight - padding (~4px top + ~4px bottom = 8px)
    float totalBarSpace = cellHeight - 8.0f;
    float healthBarHeight = std::max(12.0f, totalBarSpace * (3.2f / 4.2f));  // 3.2 / (3.2 + 1.0)
    float powerBarHeight = std::max(4.0f, totalBarSpace * (1.0f / 4.2f));    // 1.0 / (3.2 + 1.0)

    // Render grid manually with positioning (top-to-bottom in 5-row columns, then left-to-right)
    for (int i = 0; i < numPlayers; ++i) {
        int col = i / rowsPerColumn;  // Which column (group of 5)
        int row = i % rowsPerColumn;  // Position within the column (0-4)

        // Position from content region start with edge padding, then add cell+padding offsets
        float xPos = contentStart.x + edgePadding + col * (cellWidth + config_.cellPadding);
        float yPos = contentStart.y + edgePadding + row * (cellHeight + config_.cellPadding);

        ImGui::SetCursorPos(ImVec2(xPos, yPos));

        if (renderPlayerCell(players[i], cellWidth, cellHeight, healthBarHeight, powerBarHeight,
                            spellCastDb, auraDb, iconLoader, currentTimeMs)) {
            clickedPlayer_ = players[i].guid;
        }
    }
}

std::vector<RaidFramePlayer> UIRaidFramePanel::collectRaidPlayers(
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
    const std::unordered_map<std::string_view, std::string_view>& guidToName)
{
    bool needsRebuild = (guidToName.size() != lastGuidToNameSize_);
    lastGuidToNameSize_ = guidToName.size();

    if (needsRebuild) {
        cachedPlayerOrder_.clear();

        struct PlayerNamePair {
            std::string guid;
            std::string name;
        };
        std::vector<PlayerNamePair> sortTemp;

        for (const auto& [guid, name] : guidToName) {
            const ActorType* typePtr = colorGen->getActorType(guid);
            if (!typePtr || *typePtr != ActorType::Player) {
                continue;
            }

            const UnitFlags* flagsPtr = colorGen->getActorFlags(guid);
            if (flagsPtr) {
                if (!flagsPtr->isFriendly()) {
                    continue;
                }
                if (flagsPtr->isOutsider()) {
                    continue;
                }
            }

            sortTemp.push_back({std::string(guid), std::string(name)});
        }

        std::sort(sortTemp.begin(), sortTemp.end(),
                  [](const PlayerNamePair& a, const PlayerNamePair& b) {
                      return a.name < b.name;
                  });

        cachedPlayerOrder_.reserve(sortTemp.size());
        for (const auto& p : sortTemp) {
            cachedPlayerOrder_.push_back(p.guid);
        }
    }

    playersBuffer_.clear();
    playersBuffer_.reserve(cachedPlayerOrder_.size());

    for (const auto& guid : cachedPlayerOrder_) {
        auto nameIt = guidToName.find(guid);
        if (nameIt == guidToName.end()) continue;

        RaidFramePlayer player;
        player.guid = guid;
        player.name = nameIt->second;

        ActorColor color = colorGen->getCachedColor(guid);
        player.r = color.r;
        player.g = color.g;
        player.b = color.b;
        player.specId = colorGen->getSpecId(guid);

        auto posIt = actorPositions.find(guid);
        player.isActive = (posIt != actorPositions.end() && posIt->second.hasPosition);

        auto resIt = actorResources.find(guid);
        if (resIt != actorResources.end() && resIt->second.hasResource) {
            player.healthPercent = resIt->second.healthPercent;
            player.currentHealth = resIt->second.currentHealth;
            player.maxHealth = resIt->second.maxHealth;
            player.powerPercent = resIt->second.powerPercent;
            player.currentPower = resIt->second.currentPower;
            player.maxPower = resIt->second.maxPower;
            player.powerType = resIt->second.powerType;
            player.isDead = (resIt->second.currentHealth == 0 && resIt->second.maxHealth > 0);
        } else {
            player.healthPercent = 1.0f;
            player.currentHealth = 0;
            player.maxHealth = 0;
            player.powerPercent = 0.0f;
            player.currentPower = 0;
            player.maxPower = 0;
            player.powerType = 0;
            player.isDead = false;
        }

        playersBuffer_.push_back(player);
    }

    return playersBuffer_;
}

bool UIRaidFramePanel::renderPlayerCell(const RaidFramePlayer& player,
                                         float cellWidth, float cellHeight,
                                         float healthBarHeight, float powerBarHeight,
                                         const SpellCastDatabase* spellCastDb,
                                         const AuraDatabase* auraDb,
                                         SpellIconLoader* iconLoader,
                                         uint32_t currentTimeMs)
{
    bool clicked = false;

    ImGui::PushID(player.guid.c_str());

    // Create a child region for the cell - use ChildBg style for background
    // Use more top padding (4.0f) to ensure content isn't clipped by border
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::BeginChild("Cell", ImVec2(cellWidth, cellHeight), true,
                      ImGuiWindowFlags_NoScrollbar);

    // Fade inactive players (not visible in current time window)
    float alpha = player.isActive ? 1.0f : 0.4f;

    // Calculate available width for bars (cell width minus internal padding)
    float barWidth = ImGui::GetContentRegionAvail().x;

    // Remember position before health bar for text overlay
    ImVec2 healthBarPos = ImGui::GetCursorPos();

    // Health bar with class color - use full available width
    renderHealthBar(player.healthPercent, player.r, player.g, player.b,
                    barWidth, healthBarHeight, player.isDead);

    // Player name: first 5 characters, centered on the health bar (UTF-8 safe)
    std::string displayName = ui::utf8Truncate(player.name, 5);

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

    // Render spell icons (replacing power bar) if enabled and data available
    if (config_.showSpellIcons && spellCastDb && auraDb && iconLoader) {
        ImGui::SetCursorPos(ImVec2(healthBarPos.x, healthBarPos.y + healthBarHeight + 1.0f));
        renderSpellIcons(player, barWidth, spellCastDb, auraDb, iconLoader, currentTimeMs);
    } else {
        // Fallback: render power bar if we have power data
        bool hasPowerData = (player.maxPower > 0 && player.powerType >= 0);
        if (hasPowerData) {
            ImGui::SetCursorPos(ImVec2(healthBarPos.x, healthBarPos.y + healthBarHeight));
            unit_frame::drawPowerBar(player.powerPercent, player.powerType, barWidth, powerBarHeight);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Check if the cell was clicked (left click to select)
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        clicked = true;
    }

    // Right-click context menu for clearing selection
    if (ImGui::BeginPopupContextItem("PlayerCellContext")) {
        if (ImGui::MenuItem(L("btn.clear_selection"))) {
            clearSelectionRequested_ = true;
        }
        ImGui::EndPopup();
    }

    // Tooltip with detailed info on hover
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(player.r, player.g, player.b, 1.0f), "%s", player.name.c_str());

        if (player.maxHealth > 0) {
            ImGui::Text(L("format.health_full"),
                        static_cast<unsigned long long>(player.currentHealth),
                        static_cast<unsigned long long>(player.maxHealth),
                        player.healthPercent * 100.0f);
        }

        if (player.maxPower > 0) {
            const char* powerName = L("power.mana");
            switch (player.powerType) {
                case 0: powerName = L("power.mana"); break;
                case 1: powerName = L("power.rage"); break;
                case 2: powerName = L("power.focus"); break;
                case 3: powerName = L("power.energy"); break;
                case 6: powerName = L("power.runic_power"); break;
                case 12: powerName = L("power.chi"); break;
            }
            ImGui::Text(L("format.power_full"),
                        powerName, player.currentPower, player.maxPower,
                        player.powerPercent * 100.0f);
        }

        if (player.isDead) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", L("status.dead"));
        }

        if (!player.isActive) {
            ImGui::TextDisabled("%s", L("status.not_visible"));
        }

        ImGui::TextDisabled("%s", L("status.click_for_gcd"));
        ImGui::EndTooltip();
    }

    ImGui::PopID();

    return clicked;
}

void UIRaidFramePanel::renderHealthBar(float healthPercent, float r, float g, float b,
                                        float width, float height, bool isDead)
{
    ImVec2 barPos = ImGui::GetCursorScreenPos();
    unit_frame::drawHealthBar(healthPercent, r, g, b, width, height, isDead);

    // Dead players get an explicit X marker (same mark as the timeline
    // death markers) so the state doesn't rely on the gray tint alone
    if (isDead) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float cx = barPos.x + width - height * 0.5f - 2.0f;
        float cy = barPos.y + height * 0.5f;
        float s = height * 0.28f;
        ImU32 markColor = IM_COL32(255, 255, 255, 220);
        drawList->AddLine(ImVec2(cx - s, cy - s), ImVec2(cx + s, cy + s), markColor, 1.5f);
        drawList->AddLine(ImVec2(cx + s, cy - s), ImVec2(cx - s, cy + s), markColor, 1.5f);
    }
}


void UIRaidFramePanel::renderSpellIcons(const RaidFramePlayer& player, float barWidth,
                                         const SpellCastDatabase* spellCastDb,
                                         const AuraDatabase* auraDb,
                                         SpellIconLoader* iconLoader,
                                         uint32_t currentTimeMs)
{
    if (player.specId == 0) {
        return;  // No spec info, cannot determine which spells to show
    }

    auto& settings = spell_assignment::SpellAssignmentSettings::instance();

    // Get spells assigned to RaidFrameAura location (both cooldowns and auras)
    auto cooldownSpells = settings.getSpellsForLocation(
        player.specId, spell_assignment::SpellDisplayLocation::RaidFrameAura);
    auto auraSpells = settings.getAurasForLocation(
        player.specId, spell_assignment::SpellDisplayLocation::RaidFrameAura);

    std::unordered_set<uint32_t> auraSet(auraSpells.begin(), auraSpells.end());

    // Combine all unique spell IDs (cooldowns + auras) for a single cache lookup
    std::vector<uint32_t> allSpellIds;
    std::unordered_set<uint32_t> seen;

    for (uint32_t id : cooldownSpells) {
        if (seen.insert(id).second) {
            allSpellIds.push_back(id);
        }
    }
    for (uint32_t id : auraSpells) {
        if (seen.insert(id).second) {
            allSpellIds.push_back(id);
        }
    }

    if (allSpellIds.empty()) {
        return;
    }

    // Spell states for all spells, from a 250ms cache
    const auto& allStates = getOrComputeSpellStates(
        player.guid, allSpellIds, spellCastDb, auraDb, currentTimeMs);

    // Build filtered display list:
    // - Pure CDs (not in auraSet): always show
    // - Auras: only show when active
    std::vector<SpellStateResult> displayStates;
    displayStates.reserve(allStates.size());

    for (const auto& state : allStates) {
        bool isAura = auraSet.count(state.spellId) > 0;

        if (!isAura) {
            // Pure cooldown - always show
            displayStates.push_back(state);
        } else if (state.state == SpellState::Active) {
            // Aura - only show when active
            displayStates.push_back(state);
        }
    }

    // Limit to max icons
    if (static_cast<int>(displayStates.size()) > config_.maxSpellIcons) {
        displayStates.resize(config_.maxSpellIcons);
    }

    if (displayStates.empty()) {
        return;
    }

    const int numIcons = static_cast<int>(displayStates.size());
    float iconSize = config_.spellIconSize;
    float spacing = config_.spellIconSpacing;
    constexpr float minIconSize = 8.0f;
    constexpr float minSpacing = 1.0f;

    float totalWidth = numIcons * iconSize + (numIcons - 1) * spacing;

    // Shrink icons if they don't fit
    if (totalWidth > barWidth && numIcons > 0) {
        // Calculate scale factor to fit: numIcons * size + (numIcons - 1) * spacing = barWidth
        // Shrink both icon size and spacing proportionally
        float scale = barWidth / totalWidth;
        iconSize = std::max(minIconSize, iconSize * scale);
        spacing = std::max(minSpacing, spacing * scale);
        totalWidth = numIcons * iconSize + (numIcons - 1) * spacing;
    }

    const float startX = std::max(0.0f, (barWidth - totalWidth) * 0.5f);

    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int i = 0; i < numIcons; ++i) {
        float iconX = cursorPos.x + startX + i * (iconSize + spacing);
        float iconY = cursorPos.y;

        ImVec2 iconMin(windowPos.x + iconX, windowPos.y + iconY);
        ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);

        drawSpellIcon(drawList, iconMin, iconMax, displayStates[i], iconLoader);
    }
}

const std::vector<SpellStateResult>& UIRaidFramePanel::getOrComputeSpellStates(
    const std::string& playerGuid,
    const std::vector<uint32_t>& spellIds,
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    uint32_t currentTimeMs)
{
    // Check if cache is still valid (within tolerance)
    // Handle potential wrap-around with signed comparison
    int32_t timeDiff = static_cast<int32_t>(currentTimeMs) - static_cast<int32_t>(spellStateCacheTime_);
    if (timeDiff < 0) timeDiff = -timeDiff;

    bool cacheValid = (static_cast<uint32_t>(timeDiff) < SPELL_STATE_CACHE_TOLERANCE_MS);

    if (!cacheValid) {
        // Time changed significantly - clear all cached states
        cachedSpellStates_.clear();
        spellStateCacheTime_ = currentTimeMs;
    }

    // Check if this player's states are cached
    auto it = cachedSpellStates_.find(playerGuid);
    if (it != cachedSpellStates_.end()) {
        return it->second;
    }

    auto states = SpellStateComputer::computeSpellStates(
        playerGuid, spellIds, spellCastDb, auraDb, currentTimeMs);
    auto [insertIt, inserted] = cachedSpellStates_.emplace(playerGuid, std::move(states));
    return insertIt->second;
}

void UIRaidFramePanel::drawSpellIcon(ImDrawList* drawList, ImVec2 iconMin, ImVec2 iconMax,
                                      const SpellStateResult& spellState, SpellIconLoader* iconLoader)
{
    ImTextureID texture = iconLoader->getSpellIcon(spellState.spellId);
    if (!texture) {
        texture = iconLoader->getDefaultIcon();
    }

    // Define colors for different states
    constexpr ImU32 colorGold = IM_COL32(200, 180, 50, 255);
    constexpr ImU32 colorGray = IM_COL32(80, 80, 80, 255);
    constexpr ImU32 colorGreen = IM_COL32(0, 200, 0, 255);
    constexpr ImU32 colorGreenGlow = IM_COL32(0, 255, 0, 100);
    constexpr ImU32 colorWhite = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 colorGrayedOut = IM_COL32(100, 100, 100, 255);
    constexpr ImU32 colorBlack = IM_COL32(0, 0, 0, 200);

    ImU32 iconTint = colorWhite;
    ImU32 borderColor = colorGold;
    bool showTimer = false;

    switch (spellState.state) {
        case SpellState::Ready:
            iconTint = colorWhite;
            borderColor = colorGold;
            break;

        case SpellState::OnCooldown:
            iconTint = colorGrayedOut;
            borderColor = colorGray;
            showTimer = true;
            break;

        case SpellState::Active:
            iconTint = colorWhite;
            borderColor = colorGreen;
            drawList->AddRect(
                ImVec2(iconMin.x - 1, iconMin.y - 1),
                ImVec2(iconMax.x + 1, iconMax.y + 1),
                colorGreenGlow, 0.0f, 0, 2.0f);
            break;
    }

    drawList->AddImage(texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), iconTint);

    drawList->AddRect(iconMin, iconMax, borderColor, 0.0f, 0, 1.0f);

    SpellIconLoader::checkWowheadLookupRect(spellState.spellId, iconMin, iconMax);

    if (showTimer && spellState.remainingMs > 0) {
        char timerText[16];
        float remainingSec = spellState.remainingMs / 1000.0f;

        if (remainingSec >= 60.0f) {
            snprintf(timerText, sizeof(timerText), "%dm", static_cast<int>(remainingSec / 60.0f));
        } else if (remainingSec >= 10.0f) {
            snprintf(timerText, sizeof(timerText), "%.0f", remainingSec);
        } else {
            snprintf(timerText, sizeof(timerText), "%.1f", remainingSec);
        }

        ImVec2 textSize = ImGui::CalcTextSize(timerText);
        float textX = iconMin.x + (iconMax.x - iconMin.x - textSize.x) * 0.5f;
        float textY = iconMin.y + (iconMax.y - iconMin.y - textSize.y) * 0.5f;

        drawList->AddText(ImVec2(textX + 1, textY + 1), colorBlack, timerText);
        drawList->AddText(ImVec2(textX, textY), colorWhite, timerText);
    }

    // Draw stack count for active auras with stacks > 1
    if (spellState.state == SpellState::Active && spellState.stacks > 1) {
        char stackText[8];
        snprintf(stackText, sizeof(stackText), "%d", spellState.stacks);

        ImVec2 textSize = ImGui::CalcTextSize(stackText);
        float textX = iconMax.x - textSize.x - 1;
        float textY = iconMax.y - textSize.y - 1;

        drawList->AddText(ImVec2(textX + 1, textY + 1), colorBlack, stackText);
        drawList->AddText(ImVec2(textX, textY), colorWhite, stackText);
    }
}

