// UIActorAppearancePanel_CustomModels.cpp
// Custom OBJ models tab and refresh functionality

#include "UI/UIActorAppearancePanel.h"
#include "UI/AwlUI/Widgets.h"
#include "Geometry/CustomShapeRegistry.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

void UIActorAppearancePanel::refreshCustomShapes() {
    if (!customRegistry_) {
        cachedCustomShapeNames_.clear();
        return;
    }

    // Refresh registry from directory (handles creates, deletes, modifications)
    customRegistry_->refreshFromDirectory(customModelsPath_);

    // Cache the names for combo rendering
    cachedCustomShapeNames_ = customRegistry_->getShapeNames();
    customShapesNeedRefresh_ = false;
}

void UIActorAppearancePanel::renderCustomModelsTab() {
    // Refresh custom shapes list on tab open if needed
    if (customShapesNeedRefresh_ && customRegistry_) {
        refreshCustomShapes();
    }

    ImGui::TextWrapped("%s", L("appearance.custom_models_desc"));
    ImGui::Spacing();

    // Display tip about aligning with facing indicator
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", L("appearance.custom_models_tip"));
    ImGui::TextWrapped("%s", L("appearance.custom_models_workflow"));
    ImGui::Separator();

    if (!customRegistry_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", L("appearance.custom_registry_not_available"));
        return;
    }

    ImGui::TextDisabled("%s: %s", L("appearance.models_folder"), customModelsPath_.c_str());

    // Refresh button
    if (awlui::Button(L("btn.refresh"), awlui::ButtonVariant::Secondary)) {
        refreshCustomShapes();
    }

    ImGui::Separator();

    if (cachedCustomShapeNames_.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", L("shape.no_custom_models"));
        ImGui::TextWrapped("%s", L("appearance.drop_obj_hint"));
        return;
    }

    // Show each custom model with rotation controls
    ImGui::BeginChild("CustomModelsList", ImVec2(0, 0), true);

    for (const auto& modelName : cachedCustomShapeNames_) {
        ImGui::PushID(modelName.c_str());

        // Model name header
        bool nodeOpen = ImGui::TreeNodeEx(modelName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (nodeOpen) {
            ModelRotation rot = customRegistry_->getModelRotation(modelName);

            bool changed = false;

            // Rotation sliders (X, Y, Z in degrees)
            ImGui::SetNextItemWidth(200.0f);
            if (awlui::SliderFloat(L("appearance.rotation_x"), &rot.x, -180.0f, 180.0f, "%.1f")) {
                changed = true;
            }
            ImGui::SetNextItemWidth(200.0f);
            if (awlui::SliderFloat(L("appearance.rotation_y"), &rot.y, -180.0f, 180.0f, "%.1f")) {
                changed = true;
            }
            ImGui::SetNextItemWidth(200.0f);
            if (awlui::SliderFloat(L("appearance.rotation_z"), &rot.z, -180.0f, 180.0f, "%.1f")) {
                changed = true;
            }

            // Z-axis offset slider (vertical position)
            ImGui::SetNextItemWidth(200.0f);
            if (awlui::SliderFloat(L("appearance.z_offset"), &rot.zOffset, -10.0f, 10.0f, "%.2f")) {
                changed = true;
            }

            ImGui::SameLine();
            if (awlui::Button(L("btn.reset"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                rot = ModelRotation{};
                changed = true;
            }

            if (changed) {
                customRegistry_->setModelRotation(modelName, rot);
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    const auto& errors = customRegistry_->getLoadErrors();
    if (!errors.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s:", L("appearance.load_errors"));
        for (const auto& error : errors) {
            ImGui::BulletText("%s: %s", error.filename.c_str(), error.errorMessage.c_str());
        }
    }
}
