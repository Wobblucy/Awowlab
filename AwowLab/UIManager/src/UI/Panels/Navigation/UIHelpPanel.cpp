#include "UI/Panels/Navigation/UIHelpPanel.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

void UIHelpPanel::render() {
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(320, 340), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(L("help.title"), &visible_, ImGuiWindowFlags_NoCollapse)) {
        // Playback section
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", L("help.playback"));
        ImGui::Separator();

        renderShortcutRow(L("help.key_space"), L("help.space"));
        renderShortcutRow(L("help.key_q"), L("help.q"));
        renderShortcutRow(L("help.key_e"), L("help.e"));

        ImGui::Spacing();
        ImGui::Spacing();

        // Speed section
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", L("help.speed"));
        ImGui::Separator();

        renderShortcutRow(L("help.key_1"), L("help.speed_1"));
        renderShortcutRow(L("help.key_2"), L("help.speed_2"));
        renderShortcutRow(L("help.key_3"), L("help.speed_3"));
        renderShortcutRow(L("help.key_4"), L("help.speed_4"));
        renderShortcutRow(L("help.key_5"), L("help.speed_5"));

        ImGui::Spacing();
        ImGui::Spacing();

        // Camera section
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", L("help.camera"));
        ImGui::Separator();

        renderShortcutRow(L("help.key_wasd"), L("help.wasd"));
        renderShortcutRow(L("help.key_r"), L("help.r"));
        renderShortcutRow(L("help.key_lmb"), L("help.lmb"));
        renderShortcutRow(L("help.key_rmb"), L("help.rmb"));
        renderShortcutRow(L("help.key_scroll"), L("help.scroll"));

        ImGui::Spacing();
        ImGui::Spacing();

        // Other section
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", L("help.other"));
        ImGui::Separator();

        renderShortcutRow(L("help.key_f1"), L("help.f1"));
        renderShortcutRow(L("help.key_f5"), L("help.f5"));

        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", L("help.close_hint"));
    }
    ImGui::End();
}

void UIHelpPanel::renderShortcutRow(const char* keys, const char* description) {
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%-12s", keys);
    ImGui::SameLine();
    ImGui::TextUnformatted(description);
}
