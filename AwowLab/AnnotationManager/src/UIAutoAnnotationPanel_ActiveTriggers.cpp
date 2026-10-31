#include "UIAutoAnnotationPanel.h"
#include "AutoAnnotationTriggerManager.h"
#include "AnnotationManager.h"
#include "Core/LocalizationManager.h"
#include "Database/SpellCastDatabase.h"
#include "Database/CombatDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/EventDatabase.h"
#include <imgui.h>
#include <map>

void UIAutoAnnotationPanel::renderActiveTriggers() {
    ImGui::Text(L("Active Triggers"));
    ImGui::Separator();

    if (!triggerManager_ || !triggerManager_->hasTriggers()) {
        ImGui::TextDisabled(L("No triggers configured"));
        return;
    }

    const auto& triggers = triggerManager_->getTriggers();

    std::map<std::string, std::vector<size_t>> triggersByCreature;
    for (size_t i = 0; i < triggers.size(); ++i) {
        const std::string& name = triggers[i].creature_name.empty() ? "Unknown" : triggers[i].creature_name;
        triggersByCreature[name].push_back(i);
    }

    for (const auto& [creatureName, indices] : triggersByCreature) {
        ImGui::PushID(creatureName.c_str());

        if (ImGui::TreeNodeEx(creatureName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t idx : indices) {
                const auto& trigger = triggers[idx];

                ImGui::PushID(static_cast<int>(trigger.id));

                bool enabled = trigger.enabled;
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    triggerManager_->toggleTrigger(trigger.id);
                }
                ImGui::SameLine();

                std::string spellName = getSpellName(trigger.spell_id);
                std::string displayText = trigger.getDisplayName(spellName);

                if (trigger.durationType == AnnotationDuration::Timed) {
                    displayText += " (" + std::to_string(trigger.durationMs / 1000) + "s)";
                } else {
                    displayText += " (Encounter)";
                }

                bool isBeingEdited = (editingTriggerId_ == trigger.id);
                if (isBeingEdited) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                }

                ImGui::TextWrapped("%s", displayText.c_str());

                if (isBeingEdited) {
                    ImGui::PopStyleColor();
                }

                if (ImGui::SmallButton("Edit")) {
                    loadTriggerForEditing(trigger);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    if (editingTriggerId_ == trigger.id) {
                        editingTriggerId_ = 0;
                    }
                    triggerManager_->removeTrigger(trigger.id);
                    ImGui::PopID();
                    ImGui::TreePop();
                    ImGui::PopID();
                    return;  // the trigger list just changed under this loop
                }

                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

void UIAutoAnnotationPanel::updatePreviewAnnotation() {
    if (!showPreview_ || selectedSpellId_ == 0) {
        clearPreviewIfNeeded();
        return;
    }

    if (!annotationManager_ || !triggerManager_) {
        return;
    }

    float anchorX = 0.0f, anchorY = 0.0f;
    bool hasPosition = triggerManager_->getFirstEventPositionForSpell(
        selectedSpellId_, configTiming_, configAnchor_,
        spellCastDb_, combatDb_, auraDb_, eventDb_,
        anchorX, anchorY
    );

    if (!hasPosition) {
        clearPreviewIfNeeded();
        return;
    }

    Annotation preview;
    preview.shape = configShape_;
    preview.color = {configColor_[0], configColor_[1], configColor_[2], configColor_[3]};
    preview.dimensions = {configDimensions_[0], configDimensions_[1],
                          configDimensions_[2], configDimensions_[3]};
    preview.pointCount = static_cast<uint8_t>(configPointCount_);
    preview.worldX = anchorX + configOffsetX_;
    preview.worldY = anchorY + configOffsetY_;
    preview.anchorMode = AnnotationAnchor::World;
    preview.durationType = AnnotationDuration::Encounter;
    preview.visible = true;

    preview.visualStyle = configVisualStyle_;
    preview.stripeSpacing = configStripeSpacing_;
    preview.rippleSpeed = configRippleSpeed_;
    preview.rippleCount = configRippleCount_;

    preview.enableEdgeFade = configEnableEdgeFade_;
    preview.enableGlow = configEnableGlow_;
    preview.enableOutline = configEnableOutline_;
    preview.enableGradient = configEnableGradient_;
    preview.edgeFadeWidth = configEdgeFadeWidth_;
    preview.outlineWidth = configOutlineWidth_;
    preview.glowIntensity = configGlowIntensity_;

    preview.useSecondaryColor = configUseSecondaryColor_;
    preview.useInnerZoneColor = configUseInnerZoneColor_;
    preview.secondaryColor = {configSecondaryColor_[0], configSecondaryColor_[1],
                              configSecondaryColor_[2], configSecondaryColor_[3]};
    preview.innerZoneColor = {configInnerZoneColor_[0], configInnerZoneColor_[1],
                              configInnerZoneColor_[2], configInnerZoneColor_[3]};

    preview.motion = configMotion_;
    preview.motion.travelTime_ms = static_cast<int32_t>(configTravelTimeSec_ * 1000.0f);

    annotationManager_->setPreviewAnnotation(preview);
}

void UIAutoAnnotationPanel::clearPreviewIfNeeded() {
    if (annotationManager_ && annotationManager_->hasPreviewAnnotation()) {
        annotationManager_->clearPreviewAnnotation();
    }
}
