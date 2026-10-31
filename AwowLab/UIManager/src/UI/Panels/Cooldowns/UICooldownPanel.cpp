#include "UI/Panels/Cooldowns/UICooldownPanel.h"
#include "UI/SpellIconLoader.h"
#include "UI/RaidNoteIconMenu.h"
#include "UI/SpellAssignmentSettings.h"
#include "Database/SpellCastDatabase.h"
#include "Database/AuraDatabase.h"
#include "Core/LocalizationManager.h"
#include "UI/Panels/Cooldowns/SpellStateIcon.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>

UICooldownPanel::UICooldownPanel() = default;

bool UICooldownPanel::render(uint32_t currentTimeMs) {
    if (!visible_) return false;

    ImGui::Begin(L("panel.cooldowns"), &visible_);

    if (selectedPlayerGuid_.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("cd.no_player"));
        ImGui::Text("%s", L("cd.select_hint"));
        ImGui::End();
        return false;
    }

    if (!spellCastDb_ || !iconLoader_) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("cd.no_data"));
        ImGui::End();
        return false;
    }

    // Get spells to display based on CDGrouping1 assignment
    std::vector<uint32_t> spells = getSpellsToDisplay();

    if (spells.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("cd.no_spells"));
        ImGui::End();
        return false;
    }

    float windowWidth = ImGui::GetContentRegionAvail().x;
    int iconsPerRow = static_cast<int>((windowWidth + iconSpacing_) / (iconSize_ + iconSpacing_));
    if (iconsPerRow < 1) iconsPerRow = 1;

    int iconIndex = 0;
    for (uint32_t spellId : spells) {
        if (iconIndex > 0 && (iconIndex % iconsPerRow) != 0) {
            ImGui::SameLine(0, iconSpacing_);
        }

        renderSpellIcon(spellId, currentTimeMs, iconSize_);
        iconIndex++;
    }

    ImGui::End();
    return true;
}

std::vector<uint32_t> UICooldownPanel::getSpellsToDisplay() const {
    // Get all spells assigned to PlayerHUDBigCD (HUD column) for selected spec
    auto& settings = spell_assignment::SpellAssignmentSettings::instance();
    return settings.getSpellsForLocation(selectedPlayerSpecId_,
                                          spell_assignment::SpellDisplayLocation::PlayerHUDBigCD);
}

void UICooldownPanel::renderSpellIcon(uint32_t spellId, uint32_t currentTimeMs, float size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Reserve space for this icon
    ImGui::Dummy(ImVec2(size, size));

    ImTextureID texture = iconLoader_ ? iconLoader_->getSpellIcon(spellId) : ImTextureID{};

    // Use shared SpellStateComputer for unified state computation
    SpellStateResult spellState = SpellStateComputer::computeSpellState(
        selectedPlayerGuid_, spellId, spellCastDb_, auraDb_, currentTimeMs);

    ImVec2 iconMin = cursorPos;
    ImVec2 iconMax = ImVec2(cursorPos.x + size, cursorPos.y + size);

    icon_overlay::drawSpellStateIcon(drawList, texture, iconMin, size, spellState);

    SpellIconLoader::checkWowheadLookupRect(spellId, iconMin, iconMax);

    // Right-click to add to raid notes (player cooldowns are not hostile)
    ui::checkRaidNoteContextMenuRect(spellId, SPELL_NAME(spellId), 0, false, iconMin, iconMax, {}, selectedPlayerGuid_);
}
