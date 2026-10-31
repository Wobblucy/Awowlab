#include "UIAutoAnnotationWizard.h"
#include "AutoAnnotationTypes.h"
#include "Core/LocalizationManager.h"
#include "Core/StringInterner.h"
#include "Database/CombatDatabase.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <set>

void UIAutoAnnotationWizard::renderStep1_TriggerSource() {
    ImGui::Text(L("wizard.source.select_spell"));
    ImGui::Spacing();

    ImGui::BeginChild("SpellSelectionTree", ImVec2(0, 0), true);

    renderCreatureSpellTree();

    renderDamageTakenSpellList();

    ImGui::EndChild();

    if (state_.selectedSpellId_ != 0) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f),
            "%s: %s (%u)", L("wizard.source.selected"),
            state_.selectedSpellName_.c_str(), state_.selectedSpellId_);
    } else if (state_.spellCategory_ == WizardSpellCategory::Actor && !state_.selectedCreatureName_.empty()) {
        const char* timingLabel = "Actor";
        switch (state_.timing_) {
            case TriggerTiming::ActorFollow: timingLabel = L("wizard.event.actor_follow"); break;
            case TriggerTiming::ActorTrail: timingLabel = L("wizard.event.actor_trail"); break;
            case TriggerTiming::ActorSpawn: timingLabel = L("wizard.event.actor_spawn"); break;
            case TriggerTiming::ActorDeath: timingLabel = L("wizard.event.actor_death"); break;
            default: break;
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f),
            "%s: %s - %s", L("wizard.source.selected"),
            state_.selectedCreatureName_.c_str(), timingLabel);
    }
}

namespace {

constexpr float kWizardControlWidth = 200.0f;

// ImGui ID offsets so a spell listed under two categories gets two distinct rows
constexpr int kCastRowIdOffset = 0;
constexpr int kDebuffRowIdOffset = 100000;
constexpr int kBuffRowIdOffset = 200000;
constexpr int kDamageRowIdOffset = 300000;
constexpr int kActorRowIdOffset = 400000;
constexpr int kDamageTakenRowIdOffset = 600000;

} // namespace

bool UIAutoAnnotationWizard::renderSpellRow(uint32_t spellId, WizardSpellCategory category, int imguiIdOffset) {
    ImGui::PushID(static_cast<int>(spellId) + imguiIdOffset);

    if (iconLoader_ && iconLoader_->renderSpellIcon(spellId, ImVec2(16, 16))) {
        ImGui::SameLine();
    }

    // Stack buffer: this runs every frame for every spell.
    char label[256];
    snprintf(label, sizeof(label), "%s (%u)", getSpellName(spellId).c_str(), spellId);
    bool selected = (state_.selectedSpellId_ == spellId && state_.spellCategory_ == category);

    bool clicked = ImGui::Selectable(label, selected);
    if (clicked) {
        state_.selectedSpellId_ = spellId;
        state_.selectedSpellName_ = getSpellName(spellId);
        state_.spellCategory_ = category;
    }

    ImGui::PopID();
    return clicked;
}

void UIAutoAnnotationWizard::renderCreatureSpellTree() {
    if (!hostileSpells_ || hostileSpells_->empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), L("wizard.source.no_spells"));
        return;
    }

    struct ActorModeInfo {
        TriggerTiming timing;
        const char* labelKey;
        const char* tooltipKey;
    };

    const ActorModeInfo actorModes[] = {
        { TriggerTiming::ActorFollow, "wizard.event.actor_follow", "wizard.tooltip.actor_follow" },
        { TriggerTiming::ActorTrail, "wizard.event.actor_trail", "wizard.tooltip.actor_trail" },
        { TriggerTiming::ActorSpawn, "wizard.event.actor_spawn", "wizard.tooltip.actor_spawn" },
        { TriggerTiming::ActorDeath, "wizard.event.actor_death", "wizard.tooltip.actor_death" }
    };

    for (const auto& creature : *hostileSpells_) {
        if (!ImGui::TreeNode(creature.creature_name.c_str())) continue;

        struct SpellGroup {
            const std::vector<uint32_t>& spellIds;
            const char* labelKey;
            WizardSpellCategory category;
            int imguiIdOffset;
        };

        const SpellGroup spellGroups[] = {
            { creature.spell_ids, "wizard.category.cast", WizardSpellCategory::Cast, kCastRowIdOffset },
            { creature.debuff_ids, "wizard.category.debuff", WizardSpellCategory::Debuff, kDebuffRowIdOffset },
            { creature.buff_ids, "wizard.category.buff", WizardSpellCategory::Buff, kBuffRowIdOffset },
            { creature.damage_ids, "wizard.category.damage", WizardSpellCategory::Damage, kDamageRowIdOffset }
        };

        for (const auto& group : spellGroups) {
            if (group.spellIds.empty() || !ImGui::TreeNode(L(group.labelKey))) continue;

            for (uint32_t spellId : group.spellIds) {
                if (renderSpellRow(spellId, group.category, group.imguiIdOffset)) {
                    state_.selectedCreatureGuid_ = creature.creature_guid;
                    state_.selectedCreatureName_ = creature.creature_name;
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode(L("wizard.category.actor"))) {
            for (const auto& mode : actorModes) {
                ImGui::PushID(static_cast<int>(mode.timing) + kActorRowIdOffset);

                bool selected = (state_.spellCategory_ == WizardSpellCategory::Actor &&
                                state_.selectedCreatureName_ == creature.creature_name &&
                                state_.timing_ == mode.timing);

                if (ImGui::Selectable(L(mode.labelKey), selected)) {
                    state_.selectedSpellId_ = 0;
                    state_.selectedSpellName_.clear();
                    state_.selectedCreatureGuid_ = creature.creature_guid;
                    state_.selectedCreatureName_ = creature.creature_name;
                    state_.spellCategory_ = WizardSpellCategory::Actor;
                    state_.timing_ = mode.timing;
                    state_.actorNameFilter_ = creature.creature_name;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L(mode.tooltipKey));
                }

                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        ImGui::TreePop();
    }
}

void UIAutoAnnotationWizard::renderDamageTakenSpellList() {
    if (!combatDb_) return;

    std::set<uint32_t> damageTakenSpellIds;
    const auto& index = combatDb_->getDamageTakenIndex();
    for (const auto& [targetGuidId, recordPtrs] : index) {
        std::string_view victimGuid = guidInterner().lookup(targetGuidId);
        if (!victimGuid.starts_with("Player-")) continue;
        for (const CombatRecord* rec : recordPtrs) {
            if (rec->spell_id > 1) {  // Skip melee (0 and 1)
                damageTakenSpellIds.insert(rec->spell_id);
            }
        }
    }

    if (damageTakenSpellIds.empty()) return;

    if (ImGui::TreeNode(L("wizard.category.damage_taken"))) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("wizard.tooltip.damage_taken"));
        }

        for (uint32_t spellId : damageTakenSpellIds) {
            // Damage taken is grouped by victim, so there is no source creature
            if (renderSpellRow(spellId, WizardSpellCategory::DamageTaken, kDamageTakenRowIdOffset)) {
                state_.selectedCreatureGuid_.clear();
                state_.selectedCreatureName_.clear();
            }
        }
        ImGui::TreePop();
    }
}

void UIAutoAnnotationWizard::renderStep2_EventType() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float controlWidth = kWizardControlWidth * scale;

    if (state_.isSpellMode()) {
        ImGui::Text(L("wizard.event.timing"));
        ImGui::Spacing();

        switch (state_.spellCategory_) {
            case WizardSpellCategory::Cast:
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), L("wizard.event.cast_info"));
                state_.timing_ = TriggerTiming::OnCastSuccess;
                break;

            case WizardSpellCategory::Damage:
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), L("wizard.event.damage_info"));
                state_.timing_ = TriggerTiming::OnDamage;
                break;

            case WizardSpellCategory::DamageTaken:
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), L("wizard.tooltip.damage_taken"));
                state_.timing_ = TriggerTiming::OnDamageTaken;
                break;

            case WizardSpellCategory::Debuff:
                ImGui::Text(L("wizard.event.debuff_timing"));
                ImGui::Spacing();

                if (ImGui::RadioButton(L("wizard.event.on_applied"), state_.timing_ == TriggerTiming::OnDebuffApplied)) {
                    state_.timing_ = TriggerTiming::OnDebuffApplied;
                }
                if (ImGui::RadioButton(L("wizard.event.while_active"), state_.timing_ == TriggerTiming::DuringDebuff)) {
                    state_.timing_ = TriggerTiming::DuringDebuff;
                }
                if (ImGui::RadioButton(L("wizard.event.on_end"), state_.timing_ == TriggerTiming::OnDebuffEnd)) {
                    state_.timing_ = TriggerTiming::OnDebuffEnd;
                }
                break;

            case WizardSpellCategory::Buff:
                ImGui::Text(L("wizard.event.buff_timing"));
                ImGui::Spacing();

                if (ImGui::RadioButton(L("wizard.event.on_applied"), state_.timing_ == TriggerTiming::OnBuffApplied)) {
                    state_.timing_ = TriggerTiming::OnBuffApplied;
                }
                if (ImGui::RadioButton(L("wizard.event.while_active"), state_.timing_ == TriggerTiming::DuringBuff)) {
                    state_.timing_ = TriggerTiming::DuringBuff;
                }
                if (ImGui::RadioButton(L("wizard.event.on_end"), state_.timing_ == TriggerTiming::OnBuffEnd)) {
                    state_.timing_ = TriggerTiming::OnBuffEnd;
                }
                break;

            case WizardSpellCategory::Actor:
                ImGui::Text(L("wizard.event.actor_mode"));
                ImGui::Spacing();

                if (ImGui::RadioButton(L("wizard.event.actor_follow"), state_.timing_ == TriggerTiming::ActorFollow)) {
                    state_.timing_ = TriggerTiming::ActorFollow;
                }
                if (ImGui::RadioButton(L("wizard.event.actor_trail"), state_.timing_ == TriggerTiming::ActorTrail)) {
                    state_.timing_ = TriggerTiming::ActorTrail;
                }
                if (ImGui::RadioButton(L("wizard.event.actor_spawn"), state_.timing_ == TriggerTiming::ActorSpawn)) {
                    state_.timing_ = TriggerTiming::ActorSpawn;
                }
                if (ImGui::RadioButton(L("wizard.event.actor_death"), state_.timing_ == TriggerTiming::ActorDeath)) {
                    state_.timing_ = TriggerTiming::ActorDeath;
                }

                if (state_.timing_ == TriggerTiming::ActorTrail) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text(L("wizard.event.trail_interval"));
                    ImGui::SetNextItemWidth(controlWidth);
                    ImGui::SliderFloat("##TrailInterval", &state_.trailIntervalSec_, 0.5f, 5.0f, "%.1f sec");
                }

                if (state_.timing_ == TriggerTiming::ActorFollow || state_.timing_ == TriggerTiming::ActorTrail) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text(L("wizard.event.death_behavior"));
                    ImGui::Spacing();

                    const char* deathBehaviors[] = {
                        L("wizard.event.death_disappear"),
                        L("wizard.event.death_persist"),
                        L("wizard.event.death_fade")
                    };
                    int deathIdx = static_cast<int>(state_.deathBehavior_);
                    ImGui::SetNextItemWidth(controlWidth);
                    if (ImGui::Combo("##DeathBehavior", &deathIdx, deathBehaviors, 3)) {
                        state_.deathBehavior_ = static_cast<ActorDeathBehavior>(deathIdx);
                    }
                }
                break;
        }
    }
}

void UIAutoAnnotationWizard::renderStep3_Positioning() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float controlWidth = kWizardControlWidth * scale;

    if (state_.isSpellMode()) {
        ImGui::Text(L("wizard.position.anchor"));
        ImGui::Spacing();

        bool showCaster = true;
        bool showTarget = true;

        if (state_.isDebuffTiming() || isDamageTakenTiming(state_.timing_)) {
            // Debuffs and damage taken always sit on the affected unit.
            showCaster = false;
        }

        if (showCaster) {
            if (ImGui::RadioButton(L("wizard.position.at_caster"), state_.anchor_ == TriggerAnchor::Caster)) {
                state_.anchor_ = TriggerAnchor::Caster;
            }
            if (ImGui::RadioButton(L("wizard.position.at_caster_facing"), state_.anchor_ == TriggerAnchor::CasterFacing)) {
                state_.anchor_ = TriggerAnchor::CasterFacing;
            }
        }

        if (showTarget) {
            if (ImGui::RadioButton(L("wizard.position.at_target"), state_.anchor_ == TriggerAnchor::Target)) {
                state_.anchor_ = TriggerAnchor::Target;
            }
            if (ImGui::RadioButton(L("wizard.position.at_target_facing"), state_.anchor_ == TriggerAnchor::TargetFacing)) {
                state_.anchor_ = TriggerAnchor::TargetFacing;
            }
        }

        if (state_.usesFacingAnchor()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.5f, 1.0f), L("wizard.position.facing_hint"));
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), L("wizard.position.actor_hint"));
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text(L("wizard.position.offset"));
    ImGui::Spacing();

    ImGui::SetNextItemWidth(controlWidth);
    ImGui::SliderFloat(L("wizard.position.offset_x"), &state_.offsetX_, -50.0f, 50.0f, "%.1f yards");

    ImGui::SetNextItemWidth(controlWidth);
    ImGui::SliderFloat(L("wizard.position.offset_y"), &state_.offsetY_, -50.0f, 50.0f, "%.1f yards");

    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("wizard.position.offset_hint"));

    if (!state_.isDuringTiming()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(L("wizard.position.lead_time"));
        ImGui::SetNextItemWidth(controlWidth);
        ImGui::SliderFloat("##LeadTime", &state_.leadTimeSec_, 0.0f, 10.0f, "%.1f sec");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("wizard.position.lead_time_hint"));
    }
}

void UIAutoAnnotationWizard::renderStep4_ShapeSize() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float controlWidth = kWizardControlWidth * scale;

    ImGui::Text(L("wizard.shape.select"));
    ImGui::Spacing();

    renderShapeSelector();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text(L("wizard.shape.dimensions"));
    ImGui::Spacing();

    switch (state_.shape_) {
        case AnnotationShape::Circle:
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.radius"), &state_.dimensions_[0], 1.0f, 50.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.height"), &state_.dimensions_[1], 0.1f, 5.0f, "%.1f");
            break;

        case AnnotationShape::Rectangle:
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.width"), &state_.dimensions_[0], 1.0f, 50.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.length"), &state_.dimensions_[1], 1.0f, 100.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.height"), &state_.dimensions_[2], 0.1f, 5.0f, "%.1f");
            if (!state_.usesFacingAnchor()) {
                ImGui::SetNextItemWidth(controlWidth);
                ImGui::SliderFloat(L("wizard.shape.rotation"), &state_.dimensions_[3], 0.0f, 360.0f, "%.0f deg");
            }
            break;

        case AnnotationShape::Cone:
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.cone_length"), &state_.dimensions_[0], 5.0f, 60.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.cone_angle"), &state_.dimensions_[1], 15.0f, 180.0f, "%.0f deg");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.height"), &state_.dimensions_[2], 0.1f, 5.0f, "%.1f");
            if (!state_.usesFacingAnchor()) {
                ImGui::SetNextItemWidth(controlWidth);
                ImGui::SliderFloat(L("wizard.shape.rotation"), &state_.dimensions_[3], 0.0f, 360.0f, "%.0f deg");
            }
            break;

        case AnnotationShape::Line:
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.line_length"), &state_.dimensions_[0], 5.0f, 100.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.line_width"), &state_.dimensions_[1], 0.5f, 10.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.height"), &state_.dimensions_[2], 0.1f, 3.0f, "%.1f");
            if (!state_.usesFacingAnchor()) {
                ImGui::SetNextItemWidth(controlWidth);
                ImGui::SliderFloat(L("wizard.shape.rotation"), &state_.dimensions_[3], 0.0f, 360.0f, "%.0f deg");
            }
            break;

        case AnnotationShape::Star:
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.ray_length"), &state_.dimensions_[0], 5.0f, 60.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.ray_width"), &state_.dimensions_[1], 0.5f, 5.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.height"), &state_.dimensions_[2], 0.1f, 3.0f, "%.1f");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderInt(L("wizard.shape.points"), &state_.pointCount_, 3, 12);
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.rotation"), &state_.dimensions_[3], 0.0f, 360.0f, "%.0f deg");
            break;

        case AnnotationShape::Donut: {
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.outer_radius"), &state_.dimensions_[0], 1.0f, 50.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            // Keeps the slider range valid when the outer radius is very small.
            float maxInner = std::max(0.5f, state_.dimensions_[0] - 0.5f);
            ImGui::SliderFloat(L("wizard.shape.inner_radius"), &state_.dimensions_[1], 0.5f, maxInner, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.shape.height"), &state_.dimensions_[2], 0.1f, 5.0f, "%.1f");
            break;
        }

        default:
            break;
    }
}

void UIAutoAnnotationWizard::setDefaultDimensions(AnnotationShape shape) {
    switch (shape) {
        case AnnotationShape::Circle:
            state_.dimensions_ = {10.0f, 0.5f, 0.0f, 0.0f};
            break;
        case AnnotationShape::Rectangle:
            state_.dimensions_ = {5.0f, 20.0f, 0.5f, 0.0f};
            break;
        case AnnotationShape::Cone:
            state_.dimensions_ = {30.0f, 90.0f, 0.5f, 0.0f};
            break;
        case AnnotationShape::Line:
            state_.dimensions_ = {30.0f, 2.0f, 0.3f, 0.0f};
            break;
        case AnnotationShape::Star:
            state_.dimensions_ = {15.0f, 1.5f, 0.3f, 0.0f};
            state_.pointCount_ = 6;
            break;
        case AnnotationShape::Donut:
            state_.dimensions_ = {15.0f, 8.0f, 0.5f, 0.0f};
            break;
        default:
            break;
    }
}

void UIAutoAnnotationWizard::renderShapeSelector() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float controlWidth = kWizardControlWidth * scale;

    bool facingOnly = state_.usesFacingAnchor();

    struct ShapeInfo {
        AnnotationShape shape;
        const char* name;
    };

    std::vector<ShapeInfo> availableShapes;
    if (!facingOnly) {
        availableShapes = {
            {AnnotationShape::Circle, L("wizard.shape.circle")},
            {AnnotationShape::Rectangle, L("wizard.shape.rectangle")},
            {AnnotationShape::Cone, L("wizard.shape.cone")},
            {AnnotationShape::Line, L("wizard.shape.line")},
            {AnnotationShape::Star, L("wizard.shape.star")},
            {AnnotationShape::Donut, L("wizard.shape.donut")}
        };
    } else {
        // Facing anchors only make sense for directional shapes.
        availableShapes = {
            {AnnotationShape::Rectangle, L("wizard.shape.rectangle")},
            {AnnotationShape::Cone, L("wizard.shape.cone")}
        };
    }

    int currentIndex = 0;
    for (size_t i = 0; i < availableShapes.size(); ++i) {
        if (availableShapes[i].shape == state_.shape_) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    const char* previewName = availableShapes[currentIndex].name;

    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::BeginCombo("##ShapeCombo", previewName)) {
        for (size_t i = 0; i < availableShapes.size(); ++i) {
            bool isSelected = (currentIndex == static_cast<int>(i));
            if (ImGui::Selectable(availableShapes[i].name, isSelected)) {
                state_.shape_ = availableShapes[i].shape;
                setDefaultDimensions(availableShapes[i].shape);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void UIAutoAnnotationWizard::renderStep5_VisualStyle() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float controlWidth = kWizardControlWidth * scale;

    ImGui::Text(L("wizard.visual.color"));
    ImGui::Spacing();

    ImGui::ColorEdit4(L("wizard.visual.base_color"), state_.color_.data(),
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text(L("wizard.visual.pattern"));
    ImGui::Spacing();

    const char* patterns[] = {
        L("wizard.visual.solid"),
        L("wizard.visual.striped"),
        L("wizard.visual.hatched"),
        L("wizard.visual.ripple"),
        L("wizard.visual.swirl")
    };

    int patternIdx = static_cast<int>(state_.visualStyle_);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("##Pattern", &patternIdx, patterns, 5)) {
        state_.visualStyle_ = static_cast<AnnotationVisualStyle>(patternIdx);
    }

    if (state_.visualStyle_ != AnnotationVisualStyle::Solid) {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(controlWidth);
        ImGui::SliderFloat(L("wizard.visual.stripe_spacing"), &state_.stripeSpacing_, 0.5f, 10.0f, "%.1f yards");

        if (state_.visualStyle_ == AnnotationVisualStyle::RippleRings) {
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.visual.ripple_speed"), &state_.rippleSpeed_, 1.0f, 20.0f, "%.1f yds/sec");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.visual.ripple_count"), &state_.rippleCount_, 1.0f, 10.0f, "%.0f cycles");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text(L("wizard.visual.motion"));
    ImGui::Spacing();

    const char* motionTypes[] = {
        L("wizard.visual.motion_none"),
        L("wizard.visual.motion_rotate"),
        L("wizard.visual.motion_projectile"),
        L("wizard.visual.motion_raindrops")
    };

    int motionIdx = static_cast<int>(state_.motion_.type);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("##Motion", &motionIdx, motionTypes, 4)) {
        state_.motion_.type = static_cast<AnnotationMotionType>(motionIdx);
    }

    ImGui::Spacing();
    switch (state_.motion_.type) {
        case AnnotationMotionType::Rotate:
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.visual.rotation_speed"), &state_.motion_.rotationSpeed_degPerSec,
                -360.0f, 360.0f, "%.0f deg/sec");
            break;

        case AnnotationMotionType::Projectile:
            {
                const char* endModes[] = {
                    L("wizard.visual.end_manual"),
                    L("wizard.visual.end_to_target"),
                    L("wizard.visual.end_in_facing")
                };
                int endModeIdx = static_cast<int>(state_.motion_.endMode);
                ImGui::SetNextItemWidth(controlWidth);
                if (ImGui::Combo(L("wizard.visual.end_mode"), &endModeIdx, endModes, 3)) {
                    state_.motion_.endMode = static_cast<ProjectileEndMode>(endModeIdx);
                }

                if (state_.motion_.endMode == ProjectileEndMode::Manual) {
                    ImGui::SetNextItemWidth(controlWidth);
                    ImGui::SliderFloat(L("wizard.visual.end_offset_x"), &state_.motion_.endOffsetX, -100.0f, 100.0f, "%.1f");
                    ImGui::SetNextItemWidth(controlWidth);
                    ImGui::SliderFloat(L("wizard.visual.end_offset_y"), &state_.motion_.endOffsetY, -100.0f, 100.0f, "%.1f");
                } else if (state_.motion_.endMode == ProjectileEndMode::InFacingDirection) {
                    ImGui::SetNextItemWidth(controlWidth);
                    ImGui::SliderFloat(L("wizard.visual.projectile_distance"), &state_.motion_.projectileDistance, 5.0f, 100.0f, "%.1f yards");
                }

                ImGui::SetNextItemWidth(controlWidth);
                ImGui::SliderFloat(L("wizard.visual.travel_time"), &state_.travelTimeSec_, 0.5f, 30.0f, "%.1f sec");

                ImGui::Checkbox(L("wizard.visual.return_to_origin"), &state_.motion_.returnToOrigin);

                const char* endBehaviors[] = {
                    L("wizard.visual.end_persist"),
                    L("wizard.visual.end_disappear")
                };
                int endBehaviorIdx = static_cast<int>(state_.motion_.endBehavior);
                ImGui::SetNextItemWidth(controlWidth);
                if (ImGui::Combo(L("wizard.visual.end_behavior"), &endBehaviorIdx, endBehaviors, 2)) {
                    state_.motion_.endBehavior = static_cast<ProjectileEndBehavior>(endBehaviorIdx);
                }
            }
            break;

        case AnnotationMotionType::Raindrops: {
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.visual.drops_per_sec"), &state_.motion_.dropsPerSecond, 1.0f, 20.0f, "%.0f");
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat(L("wizard.visual.drop_radius"), &state_.motion_.dropRadius, 0.5f, 5.0f, "%.1f yards");
            ImGui::SetNextItemWidth(controlWidth);
            float dropLifetimeSec = state_.motion_.dropLifetime_ms / 1000.0f;
            if (ImGui::SliderFloat(L("wizard.visual.drop_lifetime"), &dropLifetimeSec, 0.5f, 2.0f, "%.1f sec")) {
                state_.motion_.dropLifetime_ms = static_cast<int32_t>(dropLifetimeSec * 1000.0f);
            }
            break;
        }

        default:
            break;
    }
}

void UIAutoAnnotationWizard::renderStep6_DurationAdvanced() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float controlWidth = kWizardControlWidth * scale;

    ImGui::Text(L("wizard.advanced.duration"));
    ImGui::Spacing();

    if (ImGui::RadioButton(L("wizard.advanced.duration_encounter"), state_.durationType_ == 0)) {
        state_.durationType_ = 0;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("wizard.advanced.duration_encounter_desc"));

    if (ImGui::RadioButton(L("wizard.advanced.duration_timed"), state_.durationType_ == 1)) {
        state_.durationType_ = 1;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("wizard.advanced.duration_timed_desc"));

    if (state_.durationType_ == 1) {
        ImGui::SetNextItemWidth(controlWidth);
        ImGui::SliderFloat(L("wizard.advanced.duration_sec"), &state_.durationSec_, 1.0f, 120.0f, "%.0f sec");
    }

    if (state_.isAuraTiming()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(L("wizard.advanced.stacks"));
        ImGui::Spacing();

        ImGui::SetNextItemWidth(100.0f * scale);
        ImGui::InputInt(L("wizard.advanced.min_stacks"), &state_.minStacks_);
        state_.minStacks_ = std::max(0, std::min(20, state_.minStacks_));

        ImGui::SetNextItemWidth(100.0f * scale);
        ImGui::InputInt(L("wizard.advanced.max_stacks"), &state_.maxStacks_);
        state_.maxStacks_ = std::max(0, std::min(20, state_.maxStacks_));

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("wizard.advanced.stacks_hint"));

        ImGui::Spacing();
        ImGui::Text(L("wizard.advanced.stack_colors"));

        ImGui::SetNextItemWidth(100.0f * scale);
        ImGui::SliderInt(L("wizard.advanced.color_count"), &state_.stackColorCount_, 0, 4);
        // Ctrl+click can type past the slider range; the loop below indexes four entries
        state_.stackColorCount_ = std::clamp(state_.stackColorCount_, 0, 4);

        for (int i = 0; i < state_.stackColorCount_; ++i) {
            ImGui::PushID(i);

            ImGui::SetNextItemWidth(60.0f * scale);
            ImGui::InputInt("##StackCount", &state_.stackThresholds_[i]);
            state_.stackThresholds_[i] = std::max(1, std::min(20, state_.stackThresholds_[i]));

            ImGui::SameLine();
            ImGui::ColorEdit4("##StackColor", state_.stackColors_[i].data(),
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf);

            ImGui::PopID();
        }
    }

    if (state_.shape_ == AnnotationShape::Circle) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(L("wizard.advanced.negation"));
        ImGui::Checkbox(L("wizard.advanced.is_negation"), &state_.isNegation_);

        if (state_.isNegation_) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("wizard.advanced.negation_hint"));

            for (size_t i = 0; i < state_.negatesSpellIds_.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%u", state_.negatesSpellIds_[i]);
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    state_.negatesSpellIds_.erase(state_.negatesSpellIds_.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            ImGui::SetNextItemWidth(100.0f * scale);
            ImGui::InputScalar("##NewNegateId", ImGuiDataType_U32, &state_.pendingNegationSpellId_);
            ImGui::SameLine();
            if (ImGui::Button(L("wizard.advanced.add_spell"))) {
                if (state_.pendingNegationSpellId_ > 0) {
                    state_.negatesSpellIds_.push_back(state_.pendingNegationSpellId_);
                    state_.pendingNegationSpellId_ = 0;
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), L("wizard.advanced.summary"));
    ImGui::Spacing();

    if (state_.isSpellMode()) {
        ImGui::BulletText("%s: %s (%u)", L("wizard.summary.spell"),
            state_.selectedSpellName_.c_str(), state_.selectedSpellId_);
    } else {
        ImGui::BulletText("%s: %s", L("wizard.summary.actor"), state_.actorNameFilter_.c_str());
    }

    ImGui::BulletText("%s: %s", L("wizard.summary.timing"), triggerTimingToString(state_.timing_));
    ImGui::BulletText("%s: %s", L("wizard.summary.shape"), annotationShapeToString(state_.shape_));

    if (state_.durationType_ == 0) {
        ImGui::BulletText("%s: %s", L("wizard.summary.duration"), L("wizard.advanced.duration_encounter"));
    } else {
        ImGui::BulletText("%s: %.0f %s", L("wizard.summary.duration"), state_.durationSec_, L("wizard.summary.seconds"));
    }
}
