#include "UI/Panels/Frames/UIPlayerBuffPanel.h"
#include "UI/Panels/Frames/AuraIconRow.h"
#include "UI/SpellAssignmentSettings.h"
#include "Color/ActorColorGenerator.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

UIPlayerBuffPanel::UIPlayerBuffPanel() = default;

void UIPlayerBuffPanel::render(
    const AuraDatabase* auraDb,
    SpellIconLoader* iconLoader,
    const ActorColorGenerator* colorGen,
    const std::string& targetGuid,
    uint32_t currentTimeMs
) {
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(300, 80), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(L("panel.player_buffs"), &visible_)) {
        ImGui::End();
        return;
    }

    if (targetGuid.empty() || !auraDb) {
        ImGui::TextDisabled("%s", L("aura.no_target"));
        ImGui::End();
        return;
    }

    uint16_t specId = colorGen ? colorGen->getSpecId(targetGuid) : 0;
    auto& settings = spell_assignment::SpellAssignmentSettings::instance();

    std::vector<const AuraInstance*> buffs;
    for (const auto* aura : auraDb->getActiveAurasOnTarget(targetGuid, currentTimeMs)) {
        if (aura->aura_type != AuraType::BUFF) continue;
        if (settings.isAuraExcludedFromPlayerBuffs(specId, aura->spell_id)) continue;
        buffs.push_back(aura);
    }

    // Buffs are the player's own casts, so they aren't offered as hostile
    constexpr icon_overlay::AuraIconStyle style{
        IM_COL32(0, 200, 0, 255),    // border
        IM_COL32(0, 255, 0, 100),    // glow
        IM_COL32(60, 120, 60, 255),  // missing icon
        false,
    };
    icon_overlay::drawAuraIconRow(buffs, currentTimeMs, iconLoader, iconSize_, iconSpacing_, style);

    ImGui::End();
}
