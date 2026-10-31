#include "UI/Panels/Settings/UISettingsHub.h"
#include "UI/UISettings.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/ActorAppearanceSettings.h"
#include "UI/NameplateSettings.h"
#include "UI/CameraSettings.h"
#include "UI/FacingIndicatorSettings.h"
#include "UI/Panels/UISettingsExportPanel.h"
#include "Camera/Camera3D.h"
#include "Core/LocalizationManager.h"
#include "Core/UnifiedSettings.h"
#include <imgui.h>

UISettingsHub::UISettingsHub() = default;
UISettingsHub::~UISettingsHub() = default;

void UISettingsHub::render() {
    if (!visible_) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 400), ImVec2(800, 700));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(L("settings.hub.title"), &visible_, flags)) {
        // Two-column layout: sidebar + content
        const float sidebarWidth = 150.0f;

        // Sidebar
        ImGui::BeginChild("SettingsSidebar", ImVec2(sidebarWidth, 0), true);
        renderSidebar();
        ImGui::EndChild();

        ImGui::SameLine();

        // Content area
        ImGui::BeginChild("SettingsContent", ImVec2(0, 0), true);
        renderContent();
        ImGui::EndChild();
    }
    ImGui::End();
}

void UISettingsHub::renderSidebar() {
    const float scale = ImGui::GetIO().FontGlobalScale;

    // Section buttons styled as selectable list items
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

    if (ImGui::Selectable(L("settings.hub.display"), isSectionSelected(Section::Display))) {
        currentSection_ = Section::Display;
    }

    // Appearance section (with sub-items)
    bool appearanceOpen = (currentSection_ == Section::Appearance);
    if (ImGui::Selectable(L("settings.hub.appearance"), appearanceOpen)) {
        currentSection_ = Section::Appearance;
    }

    if (currentSection_ == Section::Appearance) {
        ImGui::Indent(15.0f * scale);

        if (ImGui::Selectable(L("settings.hub.actors"),
                              currentAppearanceTab_ == AppearanceTab::Actors)) {
            currentAppearanceTab_ = AppearanceTab::Actors;
        }

        if (ImGui::Selectable(L("settings.hub.nameplates"),
                              currentAppearanceTab_ == AppearanceTab::Nameplates)) {
            currentAppearanceTab_ = AppearanceTab::Nameplates;
        }

        if (ImGui::Selectable(L("settings.hub.facing"),
                              currentAppearanceTab_ == AppearanceTab::Facing)) {
            currentAppearanceTab_ = AppearanceTab::Facing;
        }

        ImGui::Unindent(15.0f * scale);
    }

    // Camera section
    if (ImGui::Selectable(L("settings.hub.camera"), isSectionSelected(Section::Camera))) {
        currentSection_ = Section::Camera;
    }

    // Import/Export section
    if (ImGui::Selectable(L("settings.hub.import_export"), isSectionSelected(Section::ImportExport))) {
        currentSection_ = Section::ImportExport;
    }

    ImGui::PopStyleVar();
}

void UISettingsHub::renderContent() {
    switch (currentSection_) {
        case Section::Display:
            renderDisplaySection();
            break;
        case Section::Appearance:
            renderAppearanceSection();
            break;
        case Section::Camera:
            renderCameraSection();
            break;
        case Section::ImportExport:
            renderImportExportSection();
            break;
    }
}

void UISettingsHub::renderDisplaySection() {
    ImGui::TextDisabled("%s", L("settings.hub.display"));
    ImGui::Separator();
    ImGui::Spacing();

    // UI Scale
    ImGui::Text("%s", L("settings.hub.ui_scale"));
    ImGui::SetNextItemWidth(200.0f);
    float& fontScale = ImGui::GetIO().FontGlobalScale;
    if (awlui::SliderFloat("##UIScale", &fontScale, 0.85f, 1.5f, "%.2fx")) {
        auto& cache = SettingsCache::instance();
        cache.get().fontScale = fontScale;
        cache.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.ui_scale"));
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Menu Bar toggle
    if (uiSettings_) {
        if (awlui::Checkbox(L("menu.show_menu_bar"), &uiSettings_->showMenuBar)) {
            markSettingsDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("tooltip.menu_bar"));
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Layout management
    ImGui::TextDisabled("%s", L("settings.hub.layout"));
    ImGui::Separator();

    if (awlui::Button(L("settings.hub.reset_layout"), awlui::ButtonVariant::Ghost)) {
        if (uiSettings_) {
            *uiSettings_ = UISettings();  // Reset to defaults
            markSettingsDirty();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.reset_layout"));
    }

    ImGui::SameLine();

    if (awlui::Button(L("settings.hub.save_layout"), awlui::ButtonVariant::Primary)) {
        if (uiSettings_) {
            uiSettings_->saveToFile();
        }
    }
}

void UISettingsHub::renderAppearanceSection() {
    switch (currentAppearanceTab_) {
        case AppearanceTab::Actors:
            renderActorsTab();
            break;
        case AppearanceTab::Nameplates:
            renderNameplatesTab();
            break;
        case AppearanceTab::Facing:
            renderFacingTab();
            break;
    }
}

void UISettingsHub::renderActorsTab() {
    ImGui::TextDisabled("%s", L("settings.hub.actors"));
    ImGui::Separator();
    ImGui::Spacing();

    if (!appearanceSettings_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Actor settings not available");
        return;
    }

    RoleDefaults& roleDefaults = appearanceSettings_->getRoleDefaults();

    // Auto-assign role shapes toggle
    if (awlui::Checkbox(L("actor.auto_assign_shapes"), &roleDefaults.autoAssignRoleShapes)) {
        markSettingsDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.auto_assign_shapes"));
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Role shape assignments
    ImGui::TextDisabled("%s", L("actor.role_shapes"));

    const char* shapeNames[] = {
        "Sphere", "Cube", "Shield", "Medic Cross", "Sword", "Bow",
        "Cone", "Cylinder", "Diamond", "Pyramid", "Star", "Class Icon"
    };
    const int shapeCount = static_cast<int>(ActorShape::COUNT);

    ImGui::SetNextItemWidth(150.0f);
    int tankShape = static_cast<int>(roleDefaults.tankShape);
    if (awlui::Combo(L("actor.tank_shape"), &tankShape, shapeNames, shapeCount)) {
        roleDefaults.tankShape = static_cast<ActorShape>(tankShape);
        markSettingsDirty();
    }

    ImGui::SetNextItemWidth(150.0f);
    int healerShape = static_cast<int>(roleDefaults.healerShape);
    if (awlui::Combo(L("actor.healer_shape"), &healerShape, shapeNames, shapeCount)) {
        roleDefaults.healerShape = static_cast<ActorShape>(healerShape);
        markSettingsDirty();
    }

    ImGui::SetNextItemWidth(150.0f);
    int meleeShape = static_cast<int>(roleDefaults.meleeDPSShape);
    if (awlui::Combo(L("actor.melee_dps_shape"), &meleeShape, shapeNames, shapeCount)) {
        roleDefaults.meleeDPSShape = static_cast<ActorShape>(meleeShape);
        markSettingsDirty();
    }

    ImGui::SetNextItemWidth(150.0f);
    int rangedShape = static_cast<int>(roleDefaults.rangedDPSShape);
    if (awlui::Combo(L("actor.ranged_dps_shape"), &rangedShape, shapeNames, shapeCount)) {
        roleDefaults.rangedDPSShape = static_cast<ActorShape>(rangedShape);
        markSettingsDirty();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Default scale
    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat(L("actor.default_scale"), &roleDefaults.defaultScale, 0.5f, 2.0f, "%.1fx")) {
        markSettingsDirty();
    }
}

void UISettingsHub::renderNameplatesTab() {
    ImGui::TextDisabled("%s", L("settings.hub.nameplates"));
    ImGui::Separator();
    ImGui::Spacing();

    if (!nameplateSettings_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Nameplate settings not available");
        return;
    }

    // Global toggles
    if (awlui::Checkbox(L("filter.show_nameplates"), &nameplateSettings_->showNameplates)) {
        markSettingsDirty();
    }

    if (awlui::Checkbox(L("filter.show_health_bars"), &nameplateSettings_->showHealthBars)) {
        markSettingsDirty();
    }

    if (awlui::Checkbox(L("menu.show_auras"), &nameplateSettings_->showAuras)) {
        markSettingsDirty();
    }

    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat(L("menu.aura_icon_size"), &nameplateSettings_->auraIconSize,
                           16.0f, 48.0f, "%.0f px")) {
        markSettingsDirty();
    }

    if (awlui::Checkbox(L("nameplate.show_last_cast"), &nameplateSettings_->showLastCast)) {
        markSettingsDirty();
    }

    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat(L("nameplate.last_cast_icon_size"), &nameplateSettings_->lastCastIconSize,
                           16.0f, 32.0f, "%.0f px")) {
        markSettingsDirty();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Player text settings
    ImGui::TextDisabled("%s", L("nameplate.tab_players"));
    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat("##PlayerFontScale", &nameplateSettings_->playerSettings.fontScale,
                           0.5f, 2.0f, "%.1fx")) {
        markSettingsDirty();
    }
    ImGui::SameLine();
    ImGui::Text("%s", L("nameplate.font_scale"));

    ImGui::Spacing();

    // Other text settings
    ImGui::TextDisabled("%s", L("nameplate.tab_other"));
    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat("##OtherFontScale", &nameplateSettings_->otherSettings.fontScale,
                           0.5f, 2.0f, "%.1fx")) {
        markSettingsDirty();
    }
    ImGui::SameLine();
    ImGui::Text("%s", L("nameplate.font_scale"));
}

void UISettingsHub::renderFacingTab() {
    ImGui::TextDisabled("%s", L("settings.hub.facing"));
    ImGui::Separator();
    ImGui::Spacing();

    if (!facingSettings_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Facing indicator settings not available");
        return;
    }

    if (awlui::Checkbox(L("facing.enabled"), &facingSettings_->enabled)) {
        markSettingsDirty();
    }

    if (facingSettings_->enabled) {
        ImGui::SetNextItemWidth(150.0f);
        int opacityPercent = static_cast<int>(facingSettings_->opacity * 100);
        if (awlui::SliderInt(L("facing.opacity"), &opacityPercent, 10, 100, "%d%%")) {
            facingSettings_->opacity = opacityPercent / 100.0f;
            markSettingsDirty();
        }

        ImGui::SetNextItemWidth(150.0f);
        if (awlui::SliderFloat(L("facing.size"), &facingSettings_->size, 0.5f, 5.0f, "%.1f yards")) {
            markSettingsDirty();
        }

        // Color picker
        float colorArr[3] = { facingSettings_->color[0], facingSettings_->color[1], facingSettings_->color[2] };
        if (ImGui::ColorEdit3(L("facing.color"), colorArr, ImGuiColorEditFlags_NoInputs)) {
            facingSettings_->color[0] = colorArr[0];
            facingSettings_->color[1] = colorArr[1];
            facingSettings_->color[2] = colorArr[2];
            markSettingsDirty();
        }
    }
}

void UISettingsHub::renderCameraSection() {
    ImGui::TextDisabled("%s", L("settings.hub.camera"));
    ImGui::Separator();
    ImGui::Spacing();

    if (!cameraSettings_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Camera settings not available");
        return;
    }

    if (awlui::Checkbox(L("camera.enable_follow"), &cameraSettings_->enableFollow)) {
        markSettingsDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.camera_follow"));
    }

    // Pan sensitivity
    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat(L("camera.pan_sensitivity"), &cameraSettings_->panSensitivity,
                           0.05f, 1.0f, "%.2f")) {
        markSettingsDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.camera_pan"));
    }

    // Follow speed
    ImGui::SetNextItemWidth(150.0f);
    if (awlui::SliderFloat(L("camera.follow_speed"), &cameraSettings_->followSpeed,
                           1.0f, 10.0f, "%.1f")) {
        markSettingsDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.camera_speed"));
    }

    // Orthographic mode toggle
    if (awlui::Checkbox(L("camera.orthographic"), &cameraSettings_->orthographicMode)) {
        markSettingsDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.camera_ortho"));
    }

    ImGui::Spacing();
    ImGui::Spacing();

    if (awlui::Button(L("camera.reset"), awlui::ButtonVariant::Secondary)) {
        cameraSettings_->resetRequested = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("tooltip.camera_reset"));
    }
}

void UISettingsHub::renderImportExportSection() {
    ImGui::TextDisabled("%s", L("settings.hub.import_export"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped("%s", L("settings.hub.import_export_desc"));

    ImGui::Spacing();
    ImGui::Spacing();

    if (awlui::Button(L("settings.hub.open_import_export"), awlui::ButtonVariant::Primary)) {
        if (settingsExportPanel_) {
            settingsExportPanel_->setVisible(true);
        }
    }
}

void UISettingsHub::markSettingsDirty() {
    SettingsCache::instance().markDirty();
}
