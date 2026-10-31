#include "UIAutoAnnotationPanel.h"
#include "AutoAnnotationTriggerManager.h"
#include "AutoAnnotationTypes.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <algorithm>
#include <iterator>

namespace {

const ImVec4 kEditingColor(0.4f, 0.8f, 1.0f, 1.0f);

void tooltipOnHover(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

// Rotation is stored in radians and edited in degrees.
void rotationSlider(float& radians) {
    float rotationDeg = radians * kAnnotationRadToDeg;
    if (ImGui::SliderFloat(L("Rotation (deg)"), &rotationDeg, 0.0f, 360.0f, "%.0f")) {
        radians = rotationDeg * kAnnotationDegToRad;
    }
}

void copyColor(const float (&from)[4], std::array<float, 4>& to) {
    std::copy(std::begin(from), std::end(from), to.begin());
}

bool isTetherTiming(TriggerTiming timing) {
    return timing == TriggerTiming::Fixate || timing == TriggerTiming::Tether;
}

} // namespace

void UIAutoAnnotationPanel::renderTriggerConfig() {
    renderConfigHeader();

    switch (selectedCategory_) {
        case SelectionCategory::Actor:  renderActorTimingControls(); break;
        case SelectionCategory::Debuff: renderDebuffTimingControls(); break;
        case SelectionCategory::Buff:   renderBuffTimingControls(); break;
        case SelectionCategory::Cast:   renderCastTimingControls(); break;
        case SelectionCategory::Damage:
            ImGui::TextDisabled("%s", L("Trigger: On Damage"));
            break;
        case SelectionCategory::DamageTaken:
            ImGui::TextDisabled("%s", L("Trigger: On Damage Taken"));
            break;
    }
    ImGui::Separator();

    renderLeadTimeControls();
    renderAnchorControls();
    renderShapeControls();
    ImGui::ColorEdit4(L("Color"), configColor_);
    renderStackControls();
    renderVisualStyleControls();
    renderMotionControls();
    renderNegationControls();

    const char* durationItems[] = { L("wizard.advanced.duration_encounter"), L("wizard.advanced.duration_timed") };
    ImGui::Combo(L("wizard.advanced.duration"), &configDurationType_, durationItems, IM_ARRAYSIZE(durationItems));

    if (configDurationType_ == 1) {
        ImGui::SliderFloat(L("wizard.advanced.duration_sec"), &configDurationSec_, 1.0f, 120.0f, "%.1f s");
    }

    ImGui::Spacing();

    if (editingTriggerId_ != 0) {
        float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ImGui::Button(L("Update Trigger"), ImVec2(buttonWidth, 0))) {
            submitForm();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("wizard.cancel"), ImVec2(buttonWidth, 0))) {
            resetForm();
        }
    } else {
        if (ImGui::Button(L("Create Trigger"), ImVec2(-1, 0))) {
            submitForm();
        }
    }

    updatePreviewAnnotation();
}

void UIAutoAnnotationPanel::renderConfigHeader() {
    const bool isEditing = (editingTriggerId_ != 0);
    if (selectedCategory_ == SelectionCategory::Actor) {
        if (isEditing) {
            ImGui::TextColored(kEditingColor, L("Editing Actor: %s"), configActorNameFilter_.c_str());
        } else {
            ImGui::Text(L("Configure Actor: %s"), selectedCreatureName_.c_str());
        }
    } else {
        std::string spellName = getSpellName(selectedSpellId_);
        if (isEditing) {
            ImGui::TextColored(kEditingColor, L("Editing: %s"), spellName.c_str());
        } else {
            ImGui::Text(L("Configure: %s"), spellName.c_str());
        }
    }

    ImGui::Checkbox(L("Show Preview"), &showPreview_);
    tooltipOnHover(L("Show annotation preview on the 3D map at the first event position"));
    ImGui::SameLine();
    ImGui::Checkbox(L("Ignore Early Pull"), &configIgnoreEarlyPull_);
    tooltipOnHover(L("Skip casts in the first 500ms of the encounter (filters fake 0:00 boss casts)"));
    ImGui::Separator();
}

// Shows the timings in `choices` as a combo. Timings outside the list show
// as the first entry until the user picks one.
bool UIAutoAnnotationPanel::timingCombo(const char* label, const TriggerTiming* choices,
                                        const char* const* names, int count) {
    int timingIdx = 0;
    for (int i = 0; i < count; ++i) {
        if (configTiming_ == choices[i]) {
            timingIdx = i;
            break;
        }
    }
    if (!ImGui::Combo(label, &timingIdx, names, count)) {
        return false;
    }
    configTiming_ = choices[timingIdx];
    return true;
}

void UIAutoAnnotationPanel::renderActorTimingControls() {
    const TriggerTiming timings[] = {
        TriggerTiming::ActorFollow, TriggerTiming::ActorTrail,
        TriggerTiming::ActorSpawn, TriggerTiming::ActorDeath
    };
    const char* names[] = { L("Follow Actor"), L("Leave Trail"), L("On Spawn"), L("On Death") };
    timingCombo(L("Trigger Mode"), timings, names, IM_ARRAYSIZE(names));

    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configTiming_) {
            case TriggerTiming::ActorFollow:
                tooltip = L("Annotation appears at actor's position as timeline progresses");
                break;
            case TriggerTiming::ActorTrail:
                tooltip = L("Leave persistent annotations at actor's position over time (void zone trail)");
                break;
            case TriggerTiming::ActorSpawn:
                tooltip = L("Single annotation when actor first appears");
                break;
            case TriggerTiming::ActorDeath:
                tooltip = L("Single annotation when actor dies");
                break;
            default:
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    if (configTiming_ == TriggerTiming::ActorTrail) {
        ImGui::SliderFloat(L("Trail Interval (sec)"), &configTrailIntervalSec_, 0.5f, 5.0f, "%.1f s");
        tooltipOnHover(L("How often to leave a new annotation (in seconds)"));
    }

    if (configTiming_ != TriggerTiming::ActorFollow && configTiming_ != TriggerTiming::ActorTrail) {
        return;
    }

    const char* deathBehaviorItems[] = {
        L("Disappear Immediately"),
        L("Persist at Death Spot"),
        L("Fade Over Time")
    };

    int deathIdx = static_cast<int>(configDeathBehavior_);
    if (ImGui::Combo(L("On Actor Death"), &deathIdx, deathBehaviorItems, IM_ARRAYSIZE(deathBehaviorItems))) {
        configDeathBehavior_ = static_cast<ActorDeathBehavior>(deathIdx);
    }
    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configDeathBehavior_) {
            case ActorDeathBehavior::DisappearImmediately:
                tooltip = L("Annotation vanishes when actor dies");
                break;
            case ActorDeathBehavior::PersistAtDeathSpot:
                tooltip = L("Annotation stays at death location (uses duration setting)");
                break;
            case ActorDeathBehavior::FadeOverTime:
                tooltip = L("Annotation fades out after actor dies");
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }
}

void UIAutoAnnotationPanel::renderDebuffTimingControls() {
    const TriggerTiming timings[] = {
        TriggerTiming::OnDebuffApplied, TriggerTiming::DuringDebuff, TriggerTiming::OnDebuffEnd,
        TriggerTiming::Fixate, TriggerTiming::Tether
    };
    const char* names[] = { L("On Applied"), L("During Debuff"), L("On End"), L("Fixate"), L("Tether") };
    if (timingCombo(L("Trigger When"), timings, names, IM_ARRAYSIZE(names))) {
        // Fixate and Tether only draw lines and cones.
        if (isTetherTiming(configTiming_) &&
            configShape_ != AnnotationShape::Line && configShape_ != AnnotationShape::Cone) {
            configShape_ = AnnotationShape::Line;
        }
    }

    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configTiming_) {
            case TriggerTiming::OnDebuffApplied:
                tooltip = L("Single annotation when debuff is first applied");
                break;
            case TriggerTiming::DuringDebuff:
                tooltip = L("Annotation follows the debuffed player while active");
                break;
            case TriggerTiming::OnDebuffEnd:
                tooltip = L("Single annotation at player's position when debuff ends");
                break;
            case TriggerTiming::Fixate:
                tooltip = L("wizard.shape.tether_dynamic_tip");
                break;
            case TriggerTiming::Tether:
                tooltip = L("wizard.shape.tether_static_tip");
                break;
            default:
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    renderSourceFilterCombo(L("wizard.source.filter_any_debuff_tip"),
                            L("wizard.source.filter_creature_debuff_tip"),
                            L("wizard.source.filter_environment_debuff_tip"));
}

void UIAutoAnnotationPanel::renderBuffTimingControls() {
    const TriggerTiming timings[] = {
        TriggerTiming::OnBuffApplied, TriggerTiming::DuringBuff, TriggerTiming::OnBuffEnd
    };
    const char* names[] = { L("On Applied"), L("During Buff"), L("On End") };
    timingCombo(L("Trigger When"), timings, names, IM_ARRAYSIZE(names));

    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configTiming_) {
            case TriggerTiming::OnBuffApplied:
                tooltip = L("Single annotation when buff is first applied");
                break;
            case TriggerTiming::DuringBuff:
                tooltip = L("Annotation follows the buffed unit while active");
                break;
            case TriggerTiming::OnBuffEnd:
                tooltip = L("Single annotation at unit's position when buff ends");
                break;
            default:
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    renderSourceFilterCombo(L("wizard.source.filter_any_buff_tip"),
                            L("wizard.source.filter_creature_buff_tip"),
                            L("wizard.source.filter_environment_buff_tip"));
}

void UIAutoAnnotationPanel::renderCastTimingControls() {
    const TriggerTiming timings[] = {
        TriggerTiming::OnCastStart, TriggerTiming::OnCastSuccess, TriggerTiming::DuringCast
    };
    const char* names[] = { L("On Cast Start"), L("On Cast Success"), L("During Cast") };
    timingCombo(L("Trigger When"), timings, names, IM_ARRAYSIZE(names));

    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configTiming_) {
            case TriggerTiming::OnCastStart:
                tooltip = L("Single annotation when cast begins (interruptible casts only)");
                break;
            case TriggerTiming::OnCastSuccess:
                tooltip = L("Single annotation when spell cast completes");
                break;
            case TriggerTiming::DuringCast:
                tooltip = L("Spawn pool annotations at caster position at intervals during cast");
                break;
            default:
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    if (configTiming_ == TriggerTiming::DuringCast) {
        ImGui::SliderFloat(L("Pool Interval (sec)"), &configTrailIntervalSec_, 0.1f, 5.0f, "%.1f s");
        tooltipOnHover(L("wizard.advanced.pool_interval_tip"));
    }
}

void UIAutoAnnotationPanel::renderSourceFilterCombo(const char* anyTip, const char* creatureTip,
                                                    const char* environmentTip) {
    const char* sourceFilterItems[] = {
        L("wizard.source.filter_any"),
        L("wizard.source.filter_creature"),
        L("wizard.source.filter_environment")
    };
    int sourceFilterIdx = static_cast<int>(configSourceFilter_);
    if (ImGui::Combo(L("wizard.source.filter_label"), &sourceFilterIdx, sourceFilterItems, IM_ARRAYSIZE(sourceFilterItems))) {
        configSourceFilter_ = static_cast<SourceFilter>(sourceFilterIdx);
    }
    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configSourceFilter_) {
            case SourceFilter::Any:              tooltip = anyTip; break;
            case SourceFilter::SpecificCreature: tooltip = creatureTip; break;
            case SourceFilter::EnvironmentOnly:  tooltip = environmentTip; break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }
}

void UIAutoAnnotationPanel::renderLeadTimeControls() {
    // Lead time has no meaning for sampled modes (During*, Fixate, Tether).
    if (configTiming_ != TriggerTiming::DuringDebuff && configTiming_ != TriggerTiming::DuringBuff &&
        !isTetherTiming(configTiming_)) {
        ImGui::SliderFloat(L("Lead Time (sec)"), &configLeadTimeSec_, 0.0f, 10.0f, "%.1f s");
        if (ImGui::IsItemHovered()) {
            const char* tooltip = (configTiming_ == TriggerTiming::DuringCast)
                ? L("wizard.advanced.pool_lead_tip")
                : (selectedCategory_ == SelectionCategory::Cast)
                    ? L("Fire annotation this many seconds BEFORE the cast completes")
                    : L("Fire annotation this many seconds BEFORE the event occurs");
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    if (configTiming_ == TriggerTiming::DuringCast) {
        ImGui::SliderFloat(L("After Cast (sec)"), &configCastExtendSec_, 0.0f, 10.0f, "%.1f s");
        tooltipOnHover(L("wizard.advanced.pool_trail_tip"));
    }
}

void UIAutoAnnotationPanel::renderAnchorControls() {
    // Fixate, Tether and DuringCast have a fixed anchor. Debuff-end, during-debuff and
    // damage-taken anchor on the affected player; during-buff and buff-end on the buffed unit.
    // Everything else gets the full list.
    if (configTiming_ == TriggerTiming::Fixate) {
        ImGui::TextDisabled("%s", L("wizard.position.anchor_source_to_target"));
        tooltipOnHover(L("wizard.position.anchor_source_to_target_tip"));
    } else if (configTiming_ == TriggerTiming::Tether) {
        ImGui::TextDisabled("%s", L("wizard.position.anchor_static_to_target"));
        tooltipOnHover(L("wizard.position.anchor_static_to_target_tip"));
    } else if (configTiming_ == TriggerTiming::DuringCast) {
        ImGui::TextDisabled("%s", L("wizard.position.anchor_at_caster"));
        tooltipOnHover(L("wizard.position.anchor_at_caster_tip"));
    } else if (configTiming_ == TriggerTiming::DuringDebuff || configTiming_ == TriggerTiming::OnDebuffEnd ||
        configTiming_ == TriggerTiming::OnDamageTaken) {
        const char* anchorItems[] = { L("At Target"), L("At Target (Facing)") };
        int anchorIdx = (configAnchor_ == TriggerAnchor::TargetFacing) ? 1 : 0;
        if (ImGui::Combo(L("Place at"), &anchorIdx, anchorItems, IM_ARRAYSIZE(anchorItems))) {
            configAnchor_ = (anchorIdx == 1) ? TriggerAnchor::TargetFacing : TriggerAnchor::Target;
        }
        tooltipOnHover(L("Use 'Facing' to orient shapes in the direction the player is looking"));
    } else if (configTiming_ == TriggerTiming::DuringBuff || configTiming_ == TriggerTiming::OnBuffEnd) {
        // The buffed unit is stored as the caster anchor, since enemy buffs are mostly self-buffs.
        const char* anchorItems[] = { L("At Buffed Unit"), L("At Buffed Unit (Facing)") };
        int anchorIdx = (configAnchor_ == TriggerAnchor::CasterFacing) ? 1 : 0;
        if (ImGui::Combo(L("Place at"), &anchorIdx, anchorItems, IM_ARRAYSIZE(anchorItems))) {
            configAnchor_ = (anchorIdx == 1) ? TriggerAnchor::CasterFacing : TriggerAnchor::Caster;
        }
        tooltipOnHover(L("Use 'Facing' to orient shapes in the direction the enemy is looking"));
    } else {
        const char* anchorItems[] = {
            L("At Caster"),
            L("At Target"),
            L("At Caster (Facing)"),
            L("At Target (Facing)"),
            L("Source -> Target")
        };
        int anchorIdx = static_cast<int>(configAnchor_);
        if (ImGui::Combo(L("Place at"), &anchorIdx, anchorItems, IM_ARRAYSIZE(anchorItems))) {
            configAnchor_ = static_cast<TriggerAnchor>(anchorIdx);
        }
        if (ImGui::IsItemHovered()) {
            const char* tooltip = "";
            switch (configAnchor_) {
                case TriggerAnchor::Caster:
                    tooltip = L("Place annotation at caster's position");
                    break;
                case TriggerAnchor::Target:
                    tooltip = L("Place annotation at target's position");
                    break;
                case TriggerAnchor::CasterFacing:
                    tooltip = L("Place at caster's position, rotate to match their facing direction");
                    break;
                case TriggerAnchor::TargetFacing:
                    tooltip = L("Place at target's position, rotate to match their facing direction");
                    break;
                case TriggerAnchor::SourceToTarget:
                    tooltip = L("Draw shape spanning from caster to target (beams, chains)");
                    break;
            }
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    if (anchorUsesFacing(configAnchor_)) {
        ImGui::SliderFloat(L("Facing Offset (deg)"), &configMotion_.facingOffset_deg, -180.0f, 180.0f, "%.0f");
        tooltipOnHover(L("Rotate the shape from the actor's facing direction. 0=forward, 90=right, -90=left, 180=backward"));
    }

    ImGui::SliderFloat(L("Offset X (yards)"), &configOffsetX_, -50.0f, 50.0f, "%.1f");
    tooltipOnHover(L("Offset from anchor in world X axis (positive = East)"));
    ImGui::SliderFloat(L("Offset Y (yards)"), &configOffsetY_, -50.0f, 50.0f, "%.1f");
    tooltipOnHover(L("Offset from anchor in world Y axis (positive = North)"));
}

void UIAutoAnnotationPanel::renderShapeControls() {
    const bool tethered = isTetherTiming(configTiming_);

    // Tethers and facing anchors only offer the shapes that can point somewhere.
    if (tethered) {
        const char* fixateShapeItems[] = { L("Line"), L("Cone") };
        int shapeIdx = (configShape_ == AnnotationShape::Cone) ? 1 : 0;
        if (ImGui::Combo(L("Shape"), &shapeIdx, fixateShapeItems, IM_ARRAYSIZE(fixateShapeItems))) {
            configShape_ = (shapeIdx == 1) ? AnnotationShape::Cone : AnnotationShape::Line;
        }
        tooltipOnHover(L("wizard.shape.tether_direction_tip"));
        if (configShape_ != AnnotationShape::Line && configShape_ != AnnotationShape::Cone) {
            configShape_ = AnnotationShape::Line;
        }
    } else if (anchorUsesFacing(configAnchor_)) {
        const char* facingShapeItems[] = { L("Rectangle"), L("Cone") };
        int facingIdx = (configShape_ == AnnotationShape::Rectangle) ? 0 : 1;
        if (ImGui::Combo(L("Shape"), &facingIdx, facingShapeItems, IM_ARRAYSIZE(facingShapeItems))) {
            configShape_ = (facingIdx == 0) ? AnnotationShape::Rectangle : AnnotationShape::Cone;
        }
        if (configShape_ != AnnotationShape::Rectangle && configShape_ != AnnotationShape::Cone) {
            configShape_ = AnnotationShape::Cone;
        }
    } else {
        const char* shapeItems[] = { L("Circle"), L("Rectangle"), L("Cone"), L("Line"), L("Star"), L("Donut") };
        int shapeIdx = static_cast<int>(configShape_);
        if (ImGui::Combo(L("Shape"), &shapeIdx, shapeItems, IM_ARRAYSIZE(shapeItems))) {
            configShape_ = static_cast<AnnotationShape>(shapeIdx);
        }
    }

    // What each dimensions_ slot means depends on the shape.
    switch (configShape_) {
        case AnnotationShape::Circle:
            ImGui::SliderFloat(L("Radius (yards)"), &configDimensions_[0], 1.0f, 50.0f, "%.1f");
            ImGui::SliderFloat(L("Height"), &configDimensions_[1], 0.1f, 5.0f, "%.1f");
            break;
        case AnnotationShape::Rectangle:
            ImGui::SliderFloat(L("Width (yards)"), &configDimensions_[0], 1.0f, 50.0f, "%.1f");
            ImGui::SliderFloat(L("Length (yards)"), &configDimensions_[1], 1.0f, 100.0f, "%.1f");
            ImGui::SliderFloat(L("Height"), &configDimensions_[2], 0.1f, 5.0f, "%.1f");
            rotationSlider(configDimensions_[3]);
            break;
        case AnnotationShape::Cone:
            ImGui::SliderFloat(L("Length (yards)"), &configDimensions_[0], 5.0f, 60.0f, "%.1f");
            ImGui::SliderFloat(L("Angle (deg)"), &configDimensions_[1], 15.0f, 180.0f, "%.0f");
            ImGui::SliderFloat(L("Height"), &configDimensions_[2], 0.1f, 5.0f, "%.1f");
            if (tethered) {
                // Fixate/Tether rotation follows the NPC-to-player line.
                ImGui::TextDisabled("%s", L("wizard.position.rotation_auto"));
            } else {
                rotationSlider(configDimensions_[3]);
            }
            break;
        case AnnotationShape::Line:
            if (tethered) {
                // Fixate/Tether length comes from the actor positions; only width is set here.
                ImGui::SliderFloat(L("Width (yards)"), &configDimensions_[1], 0.5f, 10.0f, "%.1f");
                tooltipOnHover(L("wizard.shape.tether_thickness_tip"));
            } else {
                ImGui::SliderFloat(L("Length (yards)"), &configDimensions_[0], 5.0f, 100.0f, "%.1f");
                ImGui::SliderFloat(L("Width (yards)"), &configDimensions_[1], 0.5f, 10.0f, "%.1f");
                ImGui::SliderFloat(L("Height"), &configDimensions_[2], 0.1f, 3.0f, "%.1f");
                rotationSlider(configDimensions_[3]);
            }
            break;
        case AnnotationShape::Star:
            ImGui::SliderFloat(L("Ray Length (yards)"), &configDimensions_[0], 5.0f, 60.0f, "%.1f");
            ImGui::SliderFloat(L("Ray Width (yards)"), &configDimensions_[1], 0.5f, 5.0f, "%.1f");
            ImGui::SliderFloat(L("Height"), &configDimensions_[2], 0.1f, 3.0f, "%.1f");
            ImGui::SliderInt(L("Points"), &configPointCount_, 3, 12);
            rotationSlider(configDimensions_[3]);
            break;
        case AnnotationShape::Donut: {
            ImGui::SliderFloat(L("Outer Radius (yards)"), &configDimensions_[0], 1.0f, 50.0f, "%.1f");
            // Keeps the slider range valid when the outer radius is very small.
            float maxInner = std::max(0.5f, configDimensions_[0] - 0.5f);
            configDimensions_[1] = std::min(configDimensions_[1], maxInner);
            ImGui::SliderFloat(L("Inner Radius (yards)"), &configDimensions_[1], 0.5f, maxInner, "%.1f");
            ImGui::SliderFloat(L("Height"), &configDimensions_[2], 0.1f, 5.0f, "%.1f");
            break;
        }
        default:
            break;
    }
}

void UIAutoAnnotationPanel::renderStackControls() {
    // Fixate and Tether are aura timings too, but get no stack options.
    bool isAuraTrigger = isAuraTiming(configTiming_) && !isTetherTiming(configTiming_);
    if (!isAuraTrigger) return;

    ImGui::Separator();
    ImGui::Text("%s", L("Stack Filtering"));

    ImGui::SliderInt(L("Min Stacks"), &configMinStacks_, 0, 20, configMinStacks_ == 0 ? L("No min") : "%d");
    tooltipOnHover(L("Only show annotation when aura has at least this many stacks (0 = no minimum)"));

    ImGui::SliderInt(L("Max Stacks"), &configMaxStacks_, 0, 20, configMaxStacks_ == 0 ? L("No max") : "%d");
    tooltipOnHover(L("Only show annotation when aura has at most this many stacks (0 = no maximum)"));

    ImGui::Separator();
    ImGui::Text("%s", L("Stack Colors"));
    tooltipOnHover(L("Change annotation color based on stack count. Higher thresholds take priority."));

    ImGui::SliderInt(L("Color Thresholds"), &configStackColorCount_, 0, kMaxStackColors);
    tooltipOnHover(L("Number of stack-based color changes (0 = use base color only)"));
    // Ctrl+click can type past the slider range; the arrays below hold kMaxStackColors
    configStackColorCount_ = std::clamp(configStackColorCount_, 0, kMaxStackColors);

    for (int i = 0; i < configStackColorCount_; ++i) {
        ImGui::PushID(i);

        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##stacks", &configStackThresholds_[i], 1, 1);
        configStackThresholds_[i] = std::clamp(configStackThresholds_[i], 1, 99);
        ImGui::SameLine();

        ImGui::ColorEdit4("##color", configStackColors_[i], ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::Text(L("at %d+ stacks"), configStackThresholds_[i]);

        ImGui::PopID();
    }

    if (configStackColorCount_ > 0) {
        ImGui::TextDisabled("%s", L("Higher thresholds take priority over lower ones"));
    }
}

void UIAutoAnnotationPanel::renderVisualStyleControls() {
    const char* styleNames[] = {
        L("wizard.visual.solid"),
        L("wizard.visual.striped"),
        L("wizard.visual.hatched"),
        L("wizard.visual.ripple"),
        L("wizard.visual.swirl")
    };
    int styleIdx = static_cast<int>(configVisualStyle_);
    if (ImGui::Combo(L("wizard.visual.pattern"), &styleIdx, styleNames, IM_ARRAYSIZE(styleNames))) {
        configVisualStyle_ = static_cast<AnnotationVisualStyle>(styleIdx);
    }
    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configVisualStyle_) {
            case AnnotationVisualStyle::Solid:
                tooltip = L("Default solid fill");
                break;
            case AnnotationVisualStyle::Striped:
                tooltip = L("Diagonal stripes (uncertainty indicator)");
                break;
            case AnnotationVisualStyle::Hatched:
                tooltip = L("Cross-hatch pattern (uncertainty indicator)");
                break;
            case AnnotationVisualStyle::RippleRings:
                tooltip = L("Animated ripples expanding outward (projectile/wave effect)");
                break;
            case AnnotationVisualStyle::Swirl:
                tooltip = L("Spiral pattern from center (saturation shift)");
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    if (configVisualStyle_ != AnnotationVisualStyle::Solid) {
        ImGui::SliderFloat(L("Stripe Spacing"), &configStripeSpacing_, 0.5f, 10.0f, "%.1f yards");
        tooltipOnHover(L("Distance between stripes/hatches in yards"));

        if (configVisualStyle_ == AnnotationVisualStyle::RippleRings) {
            ImGui::SliderFloat(L("Ring Speed"), &configRippleSpeed_, 1.0f, 20.0f, "%.1f yards/s");
            tooltipOnHover(L("How fast rings expand outward"));
            ImGui::SliderFloat(L("Ring Count"), &configRippleCount_, 1.0f, 10.0f, "%.1f");
            tooltipOnHover(L("Number of visible ring cycles"));
        }
    }

    ImGui::Separator();
    ImGui::Text("%s", L("wizard.visual.polish"));
    ImGui::Spacing();

    ImGui::Checkbox(L("wizard.visual.edge_fade"), &configEnableEdgeFade_);
    tooltipOnHover(L("wizard.visual.edge_fade_tip"));
    if (configEnableEdgeFade_) {
        ImGui::Indent();
        ImGui::SliderFloat(L("wizard.visual.fade_width"), &configEdgeFadeWidth_, 0.01f, 0.4f, "%.2f");
        ImGui::Unindent();
    }

    ImGui::Checkbox(L("wizard.visual.outline"), &configEnableOutline_);
    tooltipOnHover(L("wizard.visual.outline_tip"));
    if (configEnableOutline_) {
        ImGui::Indent();
        ImGui::SliderFloat(L("wizard.visual.outline_width"), &configOutlineWidth_, 0.01f, 0.2f, "%.2f");
        ImGui::Unindent();
    }

    ImGui::Checkbox(L("wizard.visual.edge_glow"), &configEnableGlow_);
    tooltipOnHover(L("wizard.visual.edge_glow_tip"));
    if (configEnableGlow_) {
        ImGui::Indent();
        ImGui::SliderFloat(L("wizard.visual.glow_intensity"), &configGlowIntensity_, 0.1f, 3.0f, "%.1f");
        ImGui::Unindent();
    }

    ImGui::Checkbox(L("wizard.visual.gradient"), &configEnableGradient_);
    tooltipOnHover(L("wizard.visual.gradient_tip"));

    ImGui::Separator();
    ImGui::Text("%s", L("wizard.visual.dual_colors"));
    ImGui::Spacing();

    ImGui::Checkbox(L("wizard.visual.use_secondary_color"), &configUseSecondaryColor_);
    tooltipOnHover(L("wizard.visual.use_secondary_color_tip"));
    if (configUseSecondaryColor_) {
        ImGui::Indent();
        ImGui::ColorEdit4(L("wizard.visual.secondary_color"), configSecondaryColor_);
        ImGui::Unindent();
    }

    ImGui::Checkbox(L("wizard.visual.use_inner_zone_color"), &configUseInnerZoneColor_);
    tooltipOnHover(L("wizard.visual.use_inner_zone_color_tip"));
    if (configUseInnerZoneColor_) {
        ImGui::Indent();
        ImGui::ColorEdit4(L("wizard.visual.inner_zone_color"), configInnerZoneColor_);
        ImGui::Unindent();
    }
}

void UIAutoAnnotationPanel::renderMotionControls() {
    ImGui::Separator();
    ImGui::Text("%s", L("Motion"));

    const char* motionTypeItems[] = { L("None"), L("Rotate"), L("Projectile"), L("Raindrops") };
    int motionIdx = static_cast<int>(configMotion_.type);
    if (ImGui::Combo(L("Motion Type"), &motionIdx, motionTypeItems, IM_ARRAYSIZE(motionTypeItems))) {
        configMotion_.type = static_cast<AnnotationMotionType>(motionIdx);
    }
    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configMotion_.type) {
            case AnnotationMotionType::None:
                tooltip = L("Static annotation (no motion)");
                break;
            case AnnotationMotionType::Rotate:
                tooltip = L("Annotation rotates around its center point");
                break;
            case AnnotationMotionType::Projectile:
                tooltip = L("Annotation moves from start to end position over time");
                break;
            case AnnotationMotionType::Raindrops:
                tooltip = L("Spawns small circles within annotation bounds (swirlie effect)");
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    switch (configMotion_.type) {
        case AnnotationMotionType::Rotate:
            ImGui::SliderFloat(L("Rotation Speed (deg/s)"), &configMotion_.rotationSpeed_degPerSec, -360.0f, 360.0f, "%.0f");
            tooltipOnHover(L("Positive = clockwise, Negative = counter-clockwise"));
            break;
        case AnnotationMotionType::Projectile:
            renderProjectileControls();
            break;
        case AnnotationMotionType::Raindrops: {
            ImGui::SliderFloat(L("Drops per Second"), &configMotion_.dropsPerSecond, 1.0f, 20.0f, "%.1f");
            tooltipOnHover(L("How many raindrops spawn each second"));

            ImGui::SliderFloat(L("Drop Radius (yards)"), &configMotion_.dropRadius, 0.5f, 5.0f, "%.1f");
            tooltipOnHover(L("Size of each raindrop circle"));

            float lifetimeSec = static_cast<float>(configMotion_.dropLifetime_ms) / 1000.0f;
            if (ImGui::SliderFloat(L("Drop Lifetime (sec)"), &lifetimeSec, 0.5f, 2.0f, "%.1f s")) {
                configMotion_.dropLifetime_ms = static_cast<int32_t>(lifetimeSec * 1000.0f);
            }
            tooltipOnHover(L("How long each drop remains visible before fading"));
            break;
        }
        default:
            break;
    }
}

void UIAutoAnnotationPanel::renderProjectileControls() {
    const char* endModeItems[] = { L("Manual"), L("To Target"), L("In Facing Direction") };
    int endModeIdx = static_cast<int>(configMotion_.endMode);
    if (ImGui::Combo(L("End Position"), &endModeIdx, endModeItems, IM_ARRAYSIZE(endModeItems))) {
        configMotion_.endMode = static_cast<ProjectileEndMode>(endModeIdx);
    }
    if (ImGui::IsItemHovered()) {
        const char* tooltip = "";
        switch (configMotion_.endMode) {
            case ProjectileEndMode::Manual:
                tooltip = L("Use fixed X/Y offsets for end position");
                break;
            case ProjectileEndMode::ToTarget:
                tooltip = L("Travel from caster to spell target position (snapshotted at cast time)");
                break;
            case ProjectileEndMode::InFacingDirection:
                tooltip = L("Travel in caster's facing direction for configured distance");
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    // The end offsets are the destination in Manual mode and a fallback otherwise.
    switch (configMotion_.endMode) {
        case ProjectileEndMode::Manual:
            ImGui::SliderFloat(L("End Offset X (yards)"), &configMotion_.endOffsetX, -100.0f, 100.0f, "%.1f");
            tooltipOnHover(L("Horizontal distance to travel (positive = East)"));
            ImGui::SliderFloat(L("End Offset Y (yards)"), &configMotion_.endOffsetY, -100.0f, 100.0f, "%.1f");
            tooltipOnHover(L("Vertical distance to travel (positive = North)"));
            break;
        case ProjectileEndMode::ToTarget:
            ImGui::TextDisabled("%s", L("Projectile travels from caster to target"));
            ImGui::TextDisabled("%s", L("Fallback offsets used if no valid target:"));
            ImGui::SliderFloat(L("Fallback X (yards)"), &configMotion_.endOffsetX, -100.0f, 100.0f, "%.1f");
            ImGui::SliderFloat(L("Fallback Y (yards)"), &configMotion_.endOffsetY, -100.0f, 100.0f, "%.1f");
            break;
        case ProjectileEndMode::InFacingDirection:
            ImGui::SliderFloat(L("Distance (yards)"), &configMotion_.projectileDistance, 5.0f, 100.0f, "%.1f");
            tooltipOnHover(L("How far the projectile travels in the caster's facing direction"));
            ImGui::SliderFloat(L("Direction Offset (deg)"), &configMotion_.facingOffset_deg, -180.0f, 180.0f, "%.0f");
            tooltipOnHover(L("Offset from caster's facing direction. 0=forward, 180=backward, 90=right, -90=left"));
            ImGui::TextDisabled("%s", L("Fallback offsets used if facing unavailable:"));
            ImGui::SliderFloat(L("Fallback X (yards)"), &configMotion_.endOffsetX, -100.0f, 100.0f, "%.1f");
            ImGui::SliderFloat(L("Fallback Y (yards)"), &configMotion_.endOffsetY, -100.0f, 100.0f, "%.1f");
            break;
    }

    ImGui::SliderFloat(L("Travel Time (sec)"), &configTravelTimeSec_, 0.5f, 30.0f, "%.1f s");
    tooltipOnHover(L("How long it takes to reach the destination"));

    ImGui::Checkbox(L("Return to Origin"), &configMotion_.returnToOrigin);
    tooltipOnHover(L("Projectile travels to destination and returns to starting position (splits travel time in half)"));

    const char* endBehaviorItems[] = { L("Persist at End"), L("Disappear at End") };
    int endIdx = static_cast<int>(configMotion_.endBehavior);
    if (ImGui::Combo(L("End Behavior"), &endIdx, endBehaviorItems, IM_ARRAYSIZE(endBehaviorItems))) {
        configMotion_.endBehavior = static_cast<ProjectileEndBehavior>(endIdx);
    }
    tooltipOnHover((configMotion_.endBehavior == ProjectileEndBehavior::Persist)
        ? L("Annotation stays at final position after reaching destination")
        : L("Annotation disappears when it reaches the destination"));
}

void UIAutoAnnotationPanel::renderNegationControls() {
    // Negation zones are circles: the radius is the area they clear.
    if (configShape_ != AnnotationShape::Circle) return;

    ImGui::Separator();
    ImGui::Text("%s", L("Negation"));

    ImGui::Checkbox(L("Acts as Negation Zone"), &configIsNegation_);
    tooltipOnHover(L("When this annotation fires, it permanently removes matching annotations within its radius"));

    if (!configIsNegation_) return;

    ImGui::Indent();
    ImGui::Text("%s", L("Spells to Negate:"));

    for (size_t i = 0; i < configNegatesSpellIds_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        uint32_t spellId = configNegatesSpellIds_[i];
        std::string label = getSpellName(spellId);
        if (label.empty()) label = std::to_string(spellId);
        ImGui::BulletText("%s (%u)", label.c_str(), spellId);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            configNegatesSpellIds_.erase(configNegatesSpellIds_.begin() + static_cast<ptrdiff_t>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputScalar("##newNegateSpell", ImGuiDataType_U32, &configNewNegateSpellId_);
    ImGui::SameLine();
    if (ImGui::Button(L("Add")) && configNewNegateSpellId_ > 0) {
        auto it = std::find(configNegatesSpellIds_.begin(), configNegatesSpellIds_.end(), configNewNegateSpellId_);
        if (it == configNegatesSpellIds_.end()) {
            configNegatesSpellIds_.push_back(configNewNegateSpellId_);
        }
        configNewNegateSpellId_ = 0;
    }
    tooltipOnHover(L("Enter spell ID of annotations to clear when this fires"));

    if (configNegatesSpellIds_.empty()) {
        ImGui::TextDisabled("%s", L("No spells configured - add spell IDs to negate"));
    }
    ImGui::Unindent();
}

AutoAnnotationTrigger UIAutoAnnotationPanel::buildTriggerFromForm() const {
    AutoAnnotationTrigger trigger;
    trigger.id = editingTriggerId_;
    trigger.category = configCategory_;
    trigger.spell_id = selectedSpellId_;
    trigger.creature_guid = "";
    trigger.creature_name = selectedCreatureName_;
    trigger.timing = configTiming_;
    trigger.anchor = configAnchor_;
    trigger.shape = configShape_;
    copyColor(configColor_, trigger.color);
    copyColor(configDimensions_, trigger.dimensions);
    trigger.pointCount = static_cast<uint8_t>(configPointCount_);
    trigger.visualStyle = configVisualStyle_;
    trigger.stripeSpacing = configStripeSpacing_;
    trigger.rippleSpeed = configRippleSpeed_;
    trigger.rippleCount = configRippleCount_;
    trigger.durationType = (configDurationType_ == 0)
        ? AnnotationDuration::Encounter
        : AnnotationDuration::Timed;
    trigger.durationMs = static_cast<int32_t>(configDurationSec_ * 1000.0f);
    trigger.leadTimeMs = static_cast<int32_t>(configLeadTimeSec_ * 1000.0f);
    trigger.ignoreEarlyPullCasts = configIgnoreEarlyPull_;
    trigger.offsetX = configOffsetX_;
    trigger.offsetY = configOffsetY_;
    trigger.enabled = true;

    trigger.actorNameFilter = configActorNameFilter_;
    // Ctrl+click lets the slider take any typed value
    trigger.trailIntervalMs = std::max(static_cast<int32_t>(configTrailIntervalSec_ * 1000.0f),
                                       kMinTrailIntervalMs);
    trigger.castExtendMs = static_cast<int32_t>(configCastExtendSec_ * 1000.0f);
    trigger.deathBehavior = configDeathBehavior_;

    trigger.sourceFilter = configSourceFilter_;

    trigger.motion = configMotion_;
    trigger.motion.travelTime_ms = static_cast<int32_t>(configTravelTimeSec_ * 1000.0f);

    trigger.minStacks = static_cast<uint8_t>(configMinStacks_);
    trigger.maxStacks = static_cast<uint8_t>(configMaxStacks_);
    trigger.stackColorCount = static_cast<uint8_t>(configStackColorCount_);
    for (int i = 0; i < kMaxStackColors; ++i) {
        trigger.stackColors[i].stackCount = static_cast<uint8_t>(configStackThresholds_[i]);
        copyColor(configStackColors_[i], trigger.stackColors[i].color);
    }

    // getColorForStacks expects descending stackCount order.
    if (trigger.stackColorCount > 1) {
        std::sort(trigger.stackColors.begin(),
                  trigger.stackColors.begin() + trigger.stackColorCount,
                  [](const auto& a, const auto& b) {
                      return a.stackCount > b.stackCount;
                  });
    }

    trigger.isNegation = configIsNegation_;
    trigger.negatesSpellIds = configNegatesSpellIds_;

    trigger.enableEdgeFade = configEnableEdgeFade_;
    trigger.enableGlow = configEnableGlow_;
    trigger.enableOutline = configEnableOutline_;
    trigger.enableGradient = configEnableGradient_;
    trigger.edgeFadeWidth = configEdgeFadeWidth_;
    trigger.outlineWidth = configOutlineWidth_;
    trigger.glowIntensity = configGlowIntensity_;

    trigger.useSecondaryColor = configUseSecondaryColor_;
    trigger.useInnerZoneColor = configUseInnerZoneColor_;
    copyColor(configSecondaryColor_, trigger.secondaryColor);
    copyColor(configInnerZoneColor_, trigger.innerZoneColor);

    return trigger;
}

void UIAutoAnnotationPanel::submitForm() {
    if (!triggerManager_) return;

    AutoAnnotationTrigger trigger = buildTriggerFromForm();
    if (editingTriggerId_ != 0) {
        triggerManager_->updateTrigger(trigger);
        editingTriggerId_ = 0;
    } else {
        triggerManager_->addTrigger(trigger);
    }
}

// Back to a blank new trigger. The selected spell, the stack colours and the
// early-pull option are left as they were.
void UIAutoAnnotationPanel::resetForm() {
    editingTriggerId_ = 0;
    configCategory_ = TriggerCategory::Spell;
    configTiming_ = TriggerTiming::OnCastSuccess;
    configAnchor_ = TriggerAnchor::Target;
    configShape_ = AnnotationShape::Circle;
    configColor_[0] = 1.0f; configColor_[1] = 0.0f; configColor_[2] = 0.0f; configColor_[3] = 0.5f;
    configDimensions_[0] = 10.0f; configDimensions_[1] = 0.5f; configDimensions_[2] = 0.0f; configDimensions_[3] = 0.0f;
    configPointCount_ = 6;
    configVisualStyle_ = AnnotationVisualStyle::Solid;
    configStripeSpacing_ = 2.0f;
    configRippleSpeed_ = 5.0f;
    configRippleCount_ = 3.0f;
    configDurationType_ = 0;
    configDurationSec_ = 30.0f;
    configLeadTimeSec_ = 0.0f;
    configOffsetX_ = 0.0f;
    configOffsetY_ = 0.0f;
    configActorNameFilter_.clear();
    configTrailIntervalSec_ = 1.0f;
    configCastExtendSec_ = 0.0f;
    configDeathBehavior_ = ActorDeathBehavior::DisappearImmediately;
    configSourceFilter_ = SourceFilter::Any;

    configMotion_ = AnnotationMotionConfig{};
    configTravelTimeSec_ = 3.0f;

    configMinStacks_ = 0;
    configMaxStacks_ = 0;
    configStackColorCount_ = 0;
    for (int i = 0; i < kMaxStackColors; ++i) {
        configStackThresholds_[i] = i + 1;
    }

    configIsNegation_ = false;
    configNegatesSpellIds_.clear();
    configNewNegateSpellId_ = 0;

    configEnableEdgeFade_ = false;
    configEnableGlow_ = false;
    configEnableOutline_ = false;
    configEnableGradient_ = false;
    configEdgeFadeWidth_ = 0.15f;
    configOutlineWidth_ = 0.05f;
    configGlowIntensity_ = 1.0f;

    configUseSecondaryColor_ = false;
    configUseInnerZoneColor_ = false;
    configSecondaryColor_[0] = 0.5f; configSecondaryColor_[1] = 0.5f;
    configSecondaryColor_[2] = 0.5f; configSecondaryColor_[3] = 1.0f;
    configInnerZoneColor_[0] = 0.0f; configInnerZoneColor_[1] = 1.0f;
    configInnerZoneColor_[2] = 0.0f; configInnerZoneColor_[3] = 0.5f;
}
