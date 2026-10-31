#include "UI/UINameplateSettingsPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

void UINameplateSettingsPanel::open() {
    isOpen_ = true;
    shouldOpenPopup_ = true;
}

void UINameplateSettingsPanel::close() {
    isOpen_ = false;
    shouldOpenPopup_ = false;
}

bool UINameplateSettingsPanel::render(NameplateSettings& settings) {
    if (!isOpen_) {
        return false;
    }

    bool settingsChanged = false;

    if (shouldOpenPopup_) {
        ImGui::OpenPopup(L("nameplate.settings_title"));
        shouldOpenPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal(L("nameplate.settings_title"), &isOpen_, ImGuiWindowFlags_NoCollapse)) {

        // Global settings section
        ImGui::TextDisabled("%s", L("nameplate.global_settings"));
        ImGui::Separator();

        if (awlui::Checkbox(L("filter.show_nameplates"), &settings.showNameplates)) {
            settingsChanged = true;
        }
        if (awlui::Checkbox(L("filter.show_health_bars"), &settings.showHealthBars)) {
            settingsChanged = true;
        }
        if (awlui::Checkbox(L("menu.show_auras"), &settings.showAuras)) {
            settingsChanged = true;
        }

        ImGui::SetNextItemWidth(150.0f);
        if (awlui::SliderFloat(L("menu.aura_icon_size"), &settings.auraIconSize, 16.0f, 48.0f, "%.0f px")) {
            settingsChanged = true;
        }

        if (awlui::Checkbox(L("nameplate.show_last_cast"), &settings.showLastCast)) {
            settingsChanged = true;
        }

        ImGui::SetNextItemWidth(150.0f);
        if (awlui::SliderFloat(L("nameplate.last_cast_icon_size"), &settings.lastCastIconSize, 16.0f, 32.0f, "%.0f px")) {
            settingsChanged = true;
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Tab bar for Player and Other categories
        if (ImGui::BeginTabBar("NameplateCategoryTabs")) {
            if (ImGui::BeginTabItem(L("nameplate.tab_players"))) {
                currentTab_ = 0;
                renderCategorySettings("player", settings.playerSettings);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(L("nameplate.tab_other"))) {
                currentTab_ = 1;
                renderCategorySettings("other", settings.otherSettings);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();

        if (awlui::Button(L("btn.close"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(120, 0))) {
            close();
        }

        ImGui::EndPopup();
    } else {
        // Popup was closed by clicking outside or pressing escape
        isOpen_ = false;
    }

    return settingsChanged;
}

void UINameplateSettingsPanel::renderCategorySettings([[maybe_unused]] const char* categoryLabel, NameplateCategorySettings& settings) {
    ImGui::Spacing();

    // Font scale
    ImGui::SetNextItemWidth(150.0f);
    awlui::SliderFloat(L("nameplate.font_scale"), &settings.fontScale, 0.5f, 2.0f, "%.1fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.nameplate_font_scale"));
    }

    awlui::Checkbox(L("nameplate.use_actor_color"), &settings.useActorColor);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.nameplate_use_actor_color"));
    }

    // Custom text color (only enabled when not using actor color)
    if (!settings.useActorColor) {
        ImGui::SameLine();
        ImGui::ColorEdit4(L("nameplate.text_color"), settings.textColor,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted(L("nameplate.text_color"));
    }

    awlui::Checkbox(L("nameplate.show_background"), &settings.showBackground);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.nameplate_show_background"));
    }

    // Background opacity (only when background is enabled)
    if (settings.showBackground) {
        ImGui::SetNextItemWidth(150.0f);
        int opacityPercent = static_cast<int>(settings.backgroundOpacity * 100);
        if (awlui::SliderInt(L("nameplate.background_opacity"), &opacityPercent, 10, 100, "%d%%")) {
            settings.backgroundOpacity = opacityPercent / 100.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("tooltip.nameplate_background_opacity"));
        }
    }

    // Name length limit (0 = full name)
    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderInt(L("nameplate.name_length"), &settings.maxNameLength, 0, 20,
                         settings.maxNameLength == 0 ? "Full" : "%d")) {
        // Value changed
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.nameplate_name_length"));
    }

    ImGui::Spacing();
}
