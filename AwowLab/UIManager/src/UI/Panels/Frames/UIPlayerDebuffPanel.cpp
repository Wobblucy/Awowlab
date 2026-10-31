#include "UI/Panels/Frames/UIPlayerDebuffPanel.h"
#include "UI/Panels/Frames/AuraIconRow.h"
#include "UI/SpellAssignmentSettings.h"
#include "Color/ActorColorGenerator.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

UIPlayerDebuffPanel::UIPlayerDebuffPanel() = default;

void UIPlayerDebuffPanel::render(
    const AuraDatabase* auraDb,
    SpellIconLoader* iconLoader,
    const ActorColorGenerator* colorGen,
    const std::string& targetGuid,
    uint32_t currentTimeMs
) {
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(300, 80), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(L("panel.player_debuffs"), &visible_)) {
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

    std::vector<const AuraInstance*> debuffs;
    for (const auto* aura : auraDb->getActiveAurasOnTarget(targetGuid, currentTimeMs)) {
        if (aura->aura_type != AuraType::DEBUFF) continue;
        if (settings.isAuraExcludedFromPlayerDebuffs(specId, aura->spell_id)) continue;
        debuffs.push_back(aura);
    }

    // Debuffs can be boss abilities, so raid notes treat them as hostile
    constexpr icon_overlay::AuraIconStyle style{
        IM_COL32(200, 50, 50, 255),   // border
        IM_COL32(255, 50, 50, 100),   // glow
        IM_COL32(120, 60, 60, 255),   // missing icon
        true,
    };
    icon_overlay::drawAuraIconRow(debuffs, currentTimeMs, iconLoader, iconSize_, iconSpacing_, style);

    ImGui::End();
}
