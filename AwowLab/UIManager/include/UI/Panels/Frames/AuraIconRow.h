#pragma once
#include "UI/Panels/IconTextOverlay.h"
#include "UI/SpellIconLoader.h"
#include "UI/RaidNoteIconMenu.h"
#include "Core/SpellNameDatabase.h"
#include "Database/AuraDatabase.h"
#include <imgui.h>
#include <vector>

namespace icon_overlay {

struct AuraIconStyle {
    ImU32 border;
    ImU32 glow;
    ImU32 missingIconFill;
    // Offered to raid notes as a hostile spell (debuffs can be boss abilities)
    bool hostileForRaidNotes;
};

inline void drawAuraIcon(const AuraInstance& aura, uint32_t currentTimeMs, SpellIconLoader* loader,
                         float size, const AuraIconStyle& style) {
    uint32_t spellId = aura.spell_id;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 iconMin = ImGui::GetCursorScreenPos();
    ImVec2 iconMax(iconMin.x + size, iconMin.y + size);

    ImGui::PushID(spellId);
    ImGui::InvisibleButton("##aura_icon", ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    SpellIconLoader::checkWowheadLookup(spellId);
    ui::checkRaidNoteContextMenu(spellId, SPELL_NAME(spellId), 0, style.hostileForRaidNotes);
    ImGui::PopID();

    drawList->AddRect(
        ImVec2(iconMin.x - 1, iconMin.y - 1),
        ImVec2(iconMax.x + 1, iconMax.y + 1),
        style.glow, 2.0f, 0, 2.0f);

    ImTextureID texture = loader ? loader->getSpellIcon(spellId) : ImTextureID{};
    if (texture) {
        drawList->AddImage(texture, iconMin, iconMax);
    } else {
        drawList->AddRectFilled(iconMin, iconMax, style.missingIconFill, 4.0f);
    }

    drawList->AddRect(iconMin, iconMax, style.border, 2.0f, 0, 2.0f);

    uint32_t remainingMs = auraRemainingMs(aura.removed_at_ms, currentTimeMs);
    if (remainingMs > 0) {
        drawCountdown(drawList, iconMin, size, remainingMs);
    }
    drawStackCount(drawList, iconMax, aura.current_stacks);

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", SPELL_NAME(spellId));
        ImGui::TextDisabled("Spell ID: %u", spellId);
        ImGui::EndTooltip();
    }
}

// Icons laid out left to right, wrapping when the next one would not fit.
// Shows a dash when there is nothing to draw.
inline void drawAuraIconRow(const std::vector<const AuraInstance*>& auras, uint32_t currentTimeMs,
                            SpellIconLoader* loader, float iconSize, float iconSpacing,
                            const AuraIconStyle& style) {
    if (auras.empty()) {
        ImGui::TextDisabled("-");
        return;
    }

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cursorX = 0.0f;
    for (const AuraInstance* aura : auras) {
        if (cursorX > 0 && (cursorX + iconSize) > windowWidth) {
            cursorX = 0.0f;
        } else if (cursorX > 0) {
            ImGui::SameLine(0, iconSpacing);
        }
        cursorX += iconSize + iconSpacing;

        drawAuraIcon(*aura, currentTimeMs, loader, iconSize, style);
    }
}

} // namespace icon_overlay
