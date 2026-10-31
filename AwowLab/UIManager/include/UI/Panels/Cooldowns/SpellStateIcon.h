#pragma once
#include "UI/Panels/IconTextOverlay.h"
#include "Core/SpellStateComputer.h"
#include <imgui.h>

namespace icon_overlay {

// A cooldown icon styled by its state: gold border when ready, dimmed with
// a countdown while on cooldown, green glow and stack count while the
// spell's aura is up. A missing texture draws a plain filled square.
inline void drawSpellStateIcon(ImDrawList* drawList, ImTextureID texture,
                               ImVec2 iconMin, float size, const SpellStateResult& spellState) {
    constexpr ImU32 colorGold = IM_COL32(200, 180, 50, 255);
    constexpr ImU32 colorGray = IM_COL32(80, 80, 80, 255);
    constexpr ImU32 colorGreen = IM_COL32(0, 200, 0, 255);
    constexpr ImU32 colorGreenGlow = IM_COL32(0, 255, 0, 100);
    constexpr ImVec4 fullTint(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr ImVec4 cooldownTint(0.4f, 0.4f, 0.4f, 1.0f);

    ImVec2 iconMax(iconMin.x + size, iconMin.y + size);

    ImVec4 tint = fullTint;
    ImU32 borderColor = colorGray;
    switch (spellState.state) {
        case SpellState::Ready:
            borderColor = colorGold;
            break;
        case SpellState::OnCooldown:
            tint = cooldownTint;
            borderColor = colorGray;
            break;
        case SpellState::Active:
            borderColor = colorGreen;
            drawList->AddRect(
                ImVec2(iconMin.x - 1, iconMin.y - 1),
                ImVec2(iconMax.x + 1, iconMax.y + 1),
                colorGreenGlow, 2.0f, 0, 2.0f);
            break;
    }

    if (texture) {
        drawList->AddImage(texture, iconMin, iconMax,
                           ImVec2(0, 0), ImVec2(1, 1),
                           ImGui::GetColorU32(tint));
    } else {
        ImU32 fallbackColor = (spellState.state == SpellState::OnCooldown)
            ? IM_COL32(60, 60, 60, 255)
            : IM_COL32(80, 80, 120, 255);
        drawList->AddRectFilled(iconMin, iconMax, fallbackColor, 4.0f);
    }

    drawList->AddRect(iconMin, iconMax, borderColor, 2.0f, 0, 2.0f);

    if (spellState.state == SpellState::OnCooldown && spellState.remainingMs > 0) {
        drawCountdown(drawList, iconMin, size, spellState.remainingMs);
    }
    if (spellState.state == SpellState::Active) {
        drawStackCount(drawList, iconMax, spellState.stacks);
    }
}

} // namespace icon_overlay
