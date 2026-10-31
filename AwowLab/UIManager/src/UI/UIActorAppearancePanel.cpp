// UIActorAppearancePanel.cpp
// Core panel logic: open/close, main render, and small tabs (RoleDefaults, Overrides)
//
// Split implementation files:
// - UIActorAppearancePanel_Individual.cpp    - Individual actor tab
// - UIActorAppearancePanel_Enemies.cpp       - Enemies tab
// - UIActorAppearancePanel_CustomModels.cpp  - Custom models tab
// - UIActorAppearancePanel_Widgets.cpp       - Reusable widget helpers

#include "UI/UIActorAppearancePanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/ActorAppearanceSettings.h"
#include "Color/ActorColorGenerator.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

void UIActorAppearancePanel::open() {
    isOpen_ = true;
    shouldOpenPopup_ = true;
}

void UIActorAppearancePanel::openForActor(const std::string& actorGuid) {
    isOpen_ = true;
    shouldOpenPopup_ = true;
    shouldSelectIndividualTab_ = true;  // Force Individual tab selection

    // Set the search filter to the actor GUID to highlight it
    if (!actorGuid.empty()) {
        snprintf(searchBuffer_, sizeof(searchBuffer_), "%s", actorGuid.c_str());
    }
}

void UIActorAppearancePanel::close() {
    isOpen_ = false;
    shouldOpenPopup_ = false;
}

bool UIActorAppearancePanel::render(ActorAppearanceSettings& settings,
                                     const std::shared_ptr<ActorColorGenerator>& colorGen,
                                     const std::unordered_map<std::string_view, std::string_view>& guidToName,
                                     const std::string& selectedPlayerGuid) {
    if (!isOpen_) {
        return false;
    }

    bool settingsChanged = false;

    // Open popup on the frame it was requested (must be in same ID stack as BeginPopupModal)
    if (shouldOpenPopup_) {
        ImGui::OpenPopup(L("appearance.title"));
        shouldOpenPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(550, 500), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal(L("appearance.title"), &isOpen_, ImGuiWindowFlags_NoCollapse)) {

        // Quick actions bar
        ImGui::BeginGroup();
        {
            if (awlui::Button(L("appearance.quick_reset_all"), awlui::ButtonVariant::Danger)) {
                ImGui::OpenPopup("ConfirmReset");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("appearance.quick_reset_all_tooltip"));
            }

            if (ImGui::BeginPopupModal("ConfirmReset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("%s", L("appearance.confirm_reset"));
                ImGui::Separator();

                if (awlui::Button(L("btn.yes"), awlui::ButtonVariant::Danger,
                                  awlui::ButtonSize::Md, ImVec2(80, 0))) {
                    auto& guidOverrides = const_cast<std::unordered_map<std::string, AppearanceOverride>&>(settings.getAllGuidOverrides());
                    guidOverrides.clear();
                    auto& nameOverrides = const_cast<std::unordered_map<std::string, AppearanceOverride>&>(settings.getAllNameGroupOverrides());
                    nameOverrides.clear();
                    settings.getRoleDefaults() = RoleDefaults{};
                    settingsChanged = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (awlui::Button(L("btn.no"), awlui::ButtonVariant::Ghost,
                                  awlui::ButtonSize::Md, ImVec2(80, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();

            if (awlui::Button(L("appearance.quick_copy"), awlui::ButtonVariant::Secondary)) {
                if (!selectedPlayerGuid.empty()) {
                    const AppearanceOverride* existing = settings.getGuidOverride(selectedPlayerGuid);
                    if (existing) {
                        clipboard_ = *existing;
                    } else {
                        clipboard_ = AppearanceOverride{};
                    }
                    hasClipboard_ = true;
                    lastSelectedGuid_ = selectedPlayerGuid;
                }
            }
            if (ImGui::IsItemHovered()) {
                if (selectedPlayerGuid.empty()) {
                    ImGui::SetTooltip("%s", L("appearance.quick_copy_no_selection"));
                } else {
                    ImGui::SetTooltip("%s", L("appearance.quick_copy_tooltip"));
                }
            }

            ImGui::SameLine();

            // Paste button (disabled if no clipboard)
            ImGui::BeginDisabled(!hasClipboard_ || selectedPlayerGuid.empty());
            if (awlui::Button(L("appearance.quick_paste"), awlui::ButtonVariant::Secondary)) {
                if (hasClipboard_ && !selectedPlayerGuid.empty()) {
                    settings.setGuidOverride(selectedPlayerGuid, clipboard_);
                    settingsChanged = true;
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!hasClipboard_) {
                    ImGui::SetTooltip("%s", L("appearance.quick_paste_no_clipboard"));
                } else if (selectedPlayerGuid.empty()) {
                    ImGui::SetTooltip("%s", L("appearance.quick_paste_no_selection"));
                } else {
                    ImGui::SetTooltip("%s", L("appearance.quick_paste_tooltip"));
                }
            }

            ImGui::SameLine();

            ImGui::BeginDisabled(!hasClipboard_);
            if (ImGui::BeginCombo("##applysimilar", L("appearance.quick_apply_similar"), ImGuiComboFlags_NoPreview)) {
                if (hasClipboard_ && colorGen) {
                    uint16_t sourceSpecId = colorGen->getSpecId(lastSelectedGuid_);
                    if (sourceSpecId > 0) {
                        std::string sourceClass = ActorAppearanceSettings::getClassFromSpecId(sourceSpecId);
                        ActorRole sourceRole = ActorAppearanceSettings::getRoleFromSpecId(sourceSpecId);

                        char classLabel[64];
                        snprintf(classLabel, sizeof(classLabel), "%s %s", L("appearance.apply_to_class"), sourceClass.c_str());
                        if (ImGui::Selectable(classLabel)) {
                            for (const auto& [guid, name] : guidToName) {
                                uint16_t specId = colorGen->getSpecId(std::string(guid));
                                if (specId > 0 && ActorAppearanceSettings::getClassFromSpecId(specId) == sourceClass) {
                                    settings.setGuidOverride(std::string(guid), clipboard_);
                                }
                            }
                            settingsChanged = true;
                        }

                        char roleLabel[64];
                        snprintf(roleLabel, sizeof(roleLabel), "%s %s", L("appearance.apply_to_role"),
                                 ActorAppearanceSettings::getRoleName(sourceRole));
                        if (ImGui::Selectable(roleLabel)) {
                            for (const auto& [guid, name] : guidToName) {
                                uint16_t specId = colorGen->getSpecId(std::string(guid));
                                if (specId > 0 && ActorAppearanceSettings::getRoleFromSpecId(specId) == sourceRole) {
                                    settings.setGuidOverride(std::string(guid), clipboard_);
                                }
                            }
                            settingsChanged = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", L("appearance.quick_apply_similar_tooltip"));
            }
        }
        ImGui::EndGroup();

        ImGui::Separator();

        // Tab bar
        if (ImGui::BeginTabBar("AppearanceTabs")) {
            if (ImGui::BeginTabItem(L("appearance.tab_role_defaults"))) {
                currentTab_ = 0;
                renderRoleDefaultsTab(settings);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(L("appearance.tab_overrides"))) {
                currentTab_ = 1;
                renderOverridesTab(settings);
                ImGui::EndTabItem();
            }

            {
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (shouldSelectIndividualTab_) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                    shouldSelectIndividualTab_ = false;
                }
                if (ImGui::BeginTabItem(L("appearance.tab_individual"), nullptr, flags)) {
                    currentTab_ = 2;
                    renderIndividualTab(settings, colorGen, guidToName, selectedPlayerGuid);
                    ImGui::EndTabItem();
                }
            }

            if (ImGui::BeginTabItem(L("appearance.tab_enemies"))) {
                currentTab_ = 3;
                renderEnemiesTab(settings, colorGen, guidToName);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(L("appearance.tab_custom_models"))) {
                currentTab_ = 4;
                renderCustomModelsTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();

        // Bottom buttons
        if (awlui::Button(L("btn.save_settings"), awlui::ButtonVariant::Primary)) {
            settings.saveToFile();
            settingsChanged = true;
        }
        ImGui::SameLine();
        if (awlui::Button(L("btn.load_settings"), awlui::ButtonVariant::Secondary)) {
            settings.loadFromFile();
            settingsChanged = true;
        }
        ImGui::SameLine();
        if (awlui::Button(L("btn.close"), awlui::ButtonVariant::Ghost)) {
            close();
        }

        ImGui::EndPopup();
    } else {
        // Popup was closed by clicking outside or pressing escape
        isOpen_ = false;
    }

    return settingsChanged;
}

void UIActorAppearancePanel::renderRoleDefaultsTab(ActorAppearanceSettings& settings) {
    RoleDefaults& defaults = settings.getRoleDefaults();

    ImGui::TextWrapped("%s", L("appearance.role_defaults_desc"));
    ImGui::Separator();

    // Auto-assign toggle
    awlui::Checkbox(L("appearance.auto_assign"), &defaults.autoAssignRoleShapes);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("appearance.auto_assign_tooltip"));
    }

    ImGui::Spacing();
    ImGui::Text("%s", L("appearance.player_shapes"));
    ImGui::Indent();

    renderShapeCombo(L("role.tank_label"), defaults.tankShape);
    renderShapeCombo(L("role.healer_label"), defaults.healerShape);
    renderShapeCombo(L("role.melee_dps_label"), defaults.meleeDPSShape);
    renderShapeCombo(L("role.ranged_dps_label"), defaults.rangedDPSShape);
    renderShapeCombo(L("role.unknown_label"), defaults.unknownPlayerShape);

    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Text("%s", L("appearance.nonplayer_shapes"));
    ImGui::Indent();

    renderShapeCombo(L("nonplayer.creatures"), defaults.creatureShape);
    renderShapeCombo(L("nonplayer.vehicles"), defaults.vehicleShape);
    renderShapeCombo(L("nonplayer.pets"), defaults.petShape);

    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Text("%s", L("appearance.global_scale"));
    renderScaleSlider(L("appearance.default_scale"), defaults.defaultScale, 0.1f, 100.0f);
}

void UIActorAppearancePanel::renderOverridesTab(ActorAppearanceSettings& settings) {
    ImGui::TextWrapped("%s", L("appearance.overrides_desc"));
    ImGui::Separator();

    // Role overrides section
    if (ImGui::CollapsingHeader(L("appearance.role_overrides"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        const ActorRole roles[] = { ActorRole::Tank, ActorRole::Healer, ActorRole::MeleeDPS, ActorRole::RangedDPS };
        for (ActorRole role : roles) {
            ImGui::PushID(static_cast<int>(role));

            const char* roleName = ActorAppearanceSettings::getRoleName(role);
            bool hasOverride = settings.hasRoleOverride(role);

            if (ImGui::TreeNode(roleName)) {
                AppearanceOverride override;
                const AppearanceOverride* existing = settings.getRoleOverride(role);
                if (existing) {
                    override = *existing;
                }

                bool changed = false;
                changed |= renderOptionalShapeCombo("Shape##role", override.shape);
                changed |= renderColorEdit("Color##role", override.r, override.g, override.b);
                changed |= renderScaleSlider("Scale##role", override.scale);

                if (changed) {
                    settings.setRoleOverride(role, override);
                }

                if (hasOverride && awlui::Button(L("appearance.clear_override"),
                                                 awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                    settings.clearRoleOverride(role);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::Unindent();
    }

    // Class overrides section
    if (ImGui::CollapsingHeader(L("appearance.class_overrides"))) {
        ImGui::Indent();

        const char* classes[] = {
            "DeathKnight", "DemonHunter", "Druid", "Evoker", "Hunter",
            "Mage", "Monk", "Paladin", "Priest", "Rogue", "Shaman", "Warlock", "Warrior"
        };

        for (const char* className : classes) {
            ImGui::PushID(className);

            if (ImGui::TreeNode(className)) {
                AppearanceOverride override;
                const AppearanceOverride* existing = settings.getClassOverride(className);
                if (existing) {
                    override = *existing;
                }

                bool changed = false;
                changed |= renderOptionalShapeCombo("Shape##class", override.shape);
                changed |= renderColorEdit("Color##class", override.r, override.g, override.b);
                changed |= renderScaleSlider("Scale##class", override.scale);

                if (changed) {
                    settings.setClassOverride(className, override);
                }

                if (settings.hasClassOverride(className) &&
                    awlui::Button(L("appearance.clear_override"),
                                  awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                    settings.clearClassOverride(className);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::Unindent();
    }
}
