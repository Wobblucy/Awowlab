#include "UI/Panels/Cooldowns/UIRaidCooldownPanel.h"
#include "UI/SpellIconLoader.h"
#include "UI/RaidNoteIconMenu.h"
#include "UI/SpellAssignmentSettings.h"
#include "Core/UIUtils.h"
#include "Database/SpellCastDatabase.h"
#include "Database/AuraDatabase.h"
#include "Color/ActorColorGenerator.h"
#include "Parser/SpecializationColors.h"
#include "Core/LocalizationManager.h"
#include "UI/Panels/Cooldowns/SpellStateIcon.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>

using namespace spell_assignment;

UIRaidCooldownPanel::UIRaidCooldownPanel() = default;

SpellDisplayLocation UIRaidCooldownPanel::getGroupingLocation() const {
    // Only CDGrouping1 is used now (Raid CD Major)
    return SpellDisplayLocation::CDGrouping1;
}

void UIRaidCooldownPanel::rebuildCache() {
    playerCooldowns_.clear();

    if (!colorGenerator_ || !guidToName_) {
        cacheValid_ = true;
        return;
    }

    auto& settings = SpellAssignmentSettings::instance();
    SpellDisplayLocation location = getGroupingLocation();

    // Collect all players with their specs and assigned spells
    std::unordered_set<std::string_view> processedGuids;

    for (const auto& [guid, name] : *guidToName_) {
        if (processedGuids.count(guid)) continue;
        processedGuids.insert(guid);

        // Get spec ID for this player (convert string_view to string for API)
        std::string guidStr(guid);
        uint16_t specId = colorGenerator_->getSpecId(guidStr);
        if (specId == 0) continue;  // Not a player or unknown spec

        // Get spells assigned to this grouping for this spec
        std::vector<uint32_t> spells = settings.getSpellsForLocation(specId, location);
        if (spells.empty()) continue;  // No spells configured for this spec/grouping

        PlayerCooldownInfo info;
        info.guid = guidStr;
        info.name = std::string(name);
        info.specId = specId;
        info.spellIds = std::move(spells);

        playerCooldowns_.push_back(std::move(info));
    }

    std::sort(playerCooldowns_.begin(), playerCooldowns_.end(),
        [](const PlayerCooldownInfo& a, const PlayerCooldownInfo& b) {
            return a.name < b.name;
        });

    cacheValid_ = true;
}

bool UIRaidCooldownPanel::render(uint32_t currentTimeMs) {
    if (!visible_) return false;

    // Determine window title based on grouping
    const char* titleKey = "panel.raid_cd1";
    switch (grouping_) {
        case 1: titleKey = "panel.raid_cd1"; break;
        case 2: titleKey = "panel.raid_cd2"; break;
        case 3: titleKey = "panel.raid_cd3"; break;
    }

    ImGui::Begin(L(titleKey), &visible_);

    if (!spellCastDb_ || !iconLoader_ || !colorGenerator_ || !guidToName_) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("raid_cd.no_data"));
        ImGui::End();
        return false;
    }

    // Check if settings have changed and invalidate cache if so
    auto& settings = SpellAssignmentSettings::instance();
    uint32_t currentVersion = settings.getVersion();
    if (currentVersion != lastSeenSettingsVersion_) {
        cacheValid_ = false;
        lastSeenSettingsVersion_ = currentVersion;
    }

    // Rebuild cache if needed
    if (!cacheValid_) {
        rebuildCache();
    }

    if (playerCooldowns_.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("raid_cd.no_spells"));
        ImGui::Text("%s", L("raid_cd.config_hint"));
        ImGui::End();
        return false;
    }

    // Render each player's cooldowns: Icon first, then truncated name
    for (const auto& player : playerCooldowns_) {
        parser::ClassColor classColor = parser::getColorFromSpecId(player.specId);
        ImVec4 nameColor(classColor.rf(), classColor.gf(), classColor.bf(), 1.0f);

        // Spell icons first
        for (size_t i = 0; i < player.spellIds.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine(0, iconSpacing_);
            }
            renderSpellIcon(player.guid, player.spellIds[i], currentTimeMs, iconSize_);
        }

        // Then truncated player name (first 4 characters, UTF-8 safe)
        ImGui::SameLine(0, 4.0f);
        std::string shortName = ui::utf8Truncate(player.name, 4);
        ImGui::TextColored(nameColor, "%s", shortName.c_str());
    }

    ImGui::End();
    return true;
}

void UIRaidCooldownPanel::renderSpellIcon(const std::string& playerGuid, uint32_t spellId,
                                           uint32_t currentTimeMs, float size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Reserve space for this icon
    ImGui::Dummy(ImVec2(size, size));

    ImTextureID texture = iconLoader_ ? iconLoader_->getSpellIcon(spellId) : ImTextureID{};

    // Use shared SpellStateComputer for unified state computation
    SpellStateResult spellState = SpellStateComputer::computeSpellState(
        playerGuid, spellId, spellCastDb_, auraDb_, currentTimeMs);

    ImVec2 iconMin = cursorPos;
    ImVec2 iconMax = ImVec2(cursorPos.x + size, cursorPos.y + size);

    icon_overlay::drawSpellStateIcon(drawList, texture, iconMin, size, spellState);

    SpellIconLoader::checkWowheadLookupRect(spellId, iconMin, iconMax);

    // Right-click to add to raid notes (player cooldowns are not hostile)
    ui::checkRaidNoteContextMenuRect(spellId, SPELL_NAME(spellId), 0, false, iconMin, iconMax, {}, playerGuid);
}
