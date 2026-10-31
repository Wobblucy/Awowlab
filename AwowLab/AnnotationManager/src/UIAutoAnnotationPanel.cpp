#include "UIAutoAnnotationPanel.h"
#include "UIAutoAnnotationWizard.h"
#include "AutoAnnotationTriggerManager.h"
#include "AnnotationManager.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include "Database/SpellCastDatabase.h"
#include "Database/CombatDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/EventDatabase.h"
#include <imgui.h>
#include <algorithm>

UIAutoAnnotationPanel::UIAutoAnnotationPanel() = default;
UIAutoAnnotationPanel::~UIAutoAnnotationPanel() = default;

void UIAutoAnnotationPanel::setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& data) {
    hostileSpells_ = data;
}

void UIAutoAnnotationPanel::setTriggerManager(AutoAnnotationTriggerManager* manager) {
    triggerManager_ = manager;
}

void UIAutoAnnotationPanel::setDatabases(const SpellCastDatabase* casts, const CombatDatabase* combat,
                                          const AuraDatabase* auras, const EventDatabase* events) {
    spellCastDb_ = casts;
    combatDb_ = combat;
    auraDb_ = auras;
    eventDb_ = events;
}

std::string UIAutoAnnotationPanel::getSpellName(uint32_t spellId) const {
    return SPELL_NAME(spellId);
}

void UIAutoAnnotationPanel::loadTriggerForEditing(const AutoAnnotationTrigger& trigger) {
    editingTriggerId_ = trigger.id;
    selectedSpellId_ = trigger.spell_id;
    configCategory_ = trigger.category;
    configTiming_ = trigger.timing;
    configAnchor_ = trigger.anchor;
    configShape_ = trigger.shape;
    configColor_[0] = trigger.color[0];
    configColor_[1] = trigger.color[1];
    configColor_[2] = trigger.color[2];
    configColor_[3] = trigger.color[3];
    configDimensions_[0] = trigger.dimensions[0];
    configDimensions_[1] = trigger.dimensions[1];
    configDimensions_[2] = trigger.dimensions[2];
    configDimensions_[3] = trigger.dimensions[3];
    configPointCount_ = static_cast<int>(trigger.pointCount);
    configVisualStyle_ = trigger.visualStyle;
    configStripeSpacing_ = trigger.stripeSpacing;
    configRippleSpeed_ = trigger.rippleSpeed;
    configRippleCount_ = trigger.rippleCount;
    configDurationType_ = (trigger.durationType == AnnotationDuration::Encounter) ? 0 : 1;
    configDurationSec_ = static_cast<float>(trigger.durationMs) / 1000.0f;
    configLeadTimeSec_ = static_cast<float>(trigger.leadTimeMs) / 1000.0f;
    configIgnoreEarlyPull_ = trigger.ignoreEarlyPullCasts;
    configOffsetX_ = trigger.offsetX;
    configOffsetY_ = trigger.offsetY;

    configActorNameFilter_ = trigger.actorNameFilter;
    configTrailIntervalSec_ = static_cast<float>(trigger.trailIntervalMs) / 1000.0f;
    configCastExtendSec_ = static_cast<float>(trigger.castExtendMs) / 1000.0f;
    configDeathBehavior_ = trigger.deathBehavior;

    configSourceFilter_ = trigger.sourceFilter;

    configMotion_ = trigger.motion;
    configTravelTimeSec_ = static_cast<float>(trigger.motion.travelTime_ms) / 1000.0f;

    configMinStacks_ = static_cast<int>(trigger.minStacks);
    configMaxStacks_ = static_cast<int>(trigger.maxStacks);
    configStackColorCount_ = static_cast<int>(trigger.stackColorCount);
    for (int i = 0; i < 4; ++i) {
        configStackThresholds_[i] = static_cast<int>(trigger.stackColors[i].stackCount);
        configStackColors_[i][0] = trigger.stackColors[i].color[0];
        configStackColors_[i][1] = trigger.stackColors[i].color[1];
        configStackColors_[i][2] = trigger.stackColors[i].color[2];
        configStackColors_[i][3] = trigger.stackColors[i].color[3];
    }

    configIsNegation_ = trigger.isNegation;
    configNegatesSpellIds_ = trigger.negatesSpellIds;
    configNewNegateSpellId_ = 0;

    configEnableEdgeFade_ = trigger.enableEdgeFade;
    configEnableGlow_ = trigger.enableGlow;
    configEnableOutline_ = trigger.enableOutline;
    configEnableGradient_ = trigger.enableGradient;
    configEdgeFadeWidth_ = trigger.edgeFadeWidth;
    configOutlineWidth_ = trigger.outlineWidth;
    configGlowIntensity_ = trigger.glowIntensity;

    configUseSecondaryColor_ = trigger.useSecondaryColor;
    configUseInnerZoneColor_ = trigger.useInnerZoneColor;
    configSecondaryColor_[0] = trigger.secondaryColor[0];
    configSecondaryColor_[1] = trigger.secondaryColor[1];
    configSecondaryColor_[2] = trigger.secondaryColor[2];
    configSecondaryColor_[3] = trigger.secondaryColor[3];
    configInnerZoneColor_[0] = trigger.innerZoneColor[0];
    configInnerZoneColor_[1] = trigger.innerZoneColor[1];
    configInnerZoneColor_[2] = trigger.innerZoneColor[2];
    configInnerZoneColor_[3] = trigger.innerZoneColor[3];

    selectedCategory_ = selectionCategoryFor(trigger.category, trigger.timing);
    selectedCreatureName_ = (trigger.category == TriggerCategory::Actor)
        ? trigger.actorNameFilter
        : trigger.creature_name;
}

bool UIAutoAnnotationPanel::render(int32_t currentTimestamp_ms) {
    (void)currentTimestamp_ms;

    if (!visible_) {
        clearPreviewIfNeeded();
        return false;
    }

    bool modified = false;

    if (wizardOpen_ && wizard_) {
        WizardResult result = wizard_->render(currentTimestamp_ms);
        if (result == WizardResult::Completed) {
            handleWizardResult();
            wizardOpen_ = false;
            modified = true;
        } else if (result == WizardResult::Cancelled) {
            wizardOpen_ = false;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(L("Auto Annotations"), &visible_)) {
        ImGui::BeginChild("LeftPane", ImVec2(280, 0), true);
        renderCreatureList();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("RightPane", ImVec2(0, 0), true);

        if (ImGui::Button(L("wizard.use_wizard"), ImVec2(-1, 0))) {
            openWizard();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool hasSelection = (selectedSpellId_ != 0) ||
                            (selectedCategory_ == SelectionCategory::Actor && !selectedCreatureName_.empty());
        if (hasSelection) {
            renderTriggerConfig();
        } else {
            ImGui::TextDisabled(L("Select a spell or actor to configure"));
        }

        ImGui::Separator();

        renderActiveTriggers();

        ImGui::EndChild();
    }
    ImGui::End();

    return modified;
}

void UIAutoAnnotationPanel::openWizard() {
    if (!wizard_) {
        wizard_ = std::make_unique<UIAutoAnnotationWizard>();
        wizard_->setDependencies(
            triggerManager_,
            annotationManager_,
            spellCastDb_,
            combatDb_,
            auraDb_,
            eventDb_,
            iconLoader_,
            &hostileSpells_
        );
    }
    wizard_->open();
    wizardOpen_ = true;
}

void UIAutoAnnotationPanel::handleWizardResult() {
    if (!wizard_ || !triggerManager_) return;

    AutoAnnotationTrigger trigger = wizard_->getState().buildTrigger();
    triggerManager_->addTrigger(trigger);

    wizard_->reset();
}

void UIAutoAnnotationPanel::navigateToSpell(uint32_t spellId) {
    visible_ = true;

    for (const auto& creature : hostileSpells_) {
        auto spellIt = std::find(creature.spell_ids.begin(), creature.spell_ids.end(), spellId);
        if (spellIt != creature.spell_ids.end()) {
            selectedSpellId_ = spellId;
            selectedCreatureName_ = creature.creature_name;
            selectedCategory_ = SelectionCategory::Cast;
            configTiming_ = creature.isInterruptible(spellId)
                ? TriggerTiming::OnCastStart
                : TriggerTiming::OnCastSuccess;
            configCategory_ = TriggerCategory::Spell;
            return;
        }

        auto debuffIt = std::find(creature.debuff_ids.begin(), creature.debuff_ids.end(), spellId);
        if (debuffIt != creature.debuff_ids.end()) {
            selectedSpellId_ = spellId;
            selectedCreatureName_ = creature.creature_name;
            selectedCategory_ = SelectionCategory::Debuff;
            configTiming_ = TriggerTiming::OnDebuffApplied;
            configCategory_ = TriggerCategory::Spell;
            return;
        }

        auto buffIt = std::find(creature.buff_ids.begin(), creature.buff_ids.end(), spellId);
        if (buffIt != creature.buff_ids.end()) {
            selectedSpellId_ = spellId;
            selectedCreatureName_ = creature.creature_name;
            selectedCategory_ = SelectionCategory::Buff;
            configTiming_ = TriggerTiming::OnBuffApplied;
            configCategory_ = TriggerCategory::Spell;
            configAnchor_ = TriggerAnchor::Caster;
            return;
        }

        auto damageIt = std::find(creature.damage_ids.begin(), creature.damage_ids.end(), spellId);
        if (damageIt != creature.damage_ids.end()) {
            selectedSpellId_ = spellId;
            selectedCreatureName_ = creature.creature_name;
            selectedCategory_ = SelectionCategory::Damage;
            configTiming_ = TriggerTiming::OnDamage;
            configCategory_ = TriggerCategory::Spell;
            return;
        }
    }

    // Not in any creature's lists, so treat it as a damage-taken spell.
    selectedSpellId_ = spellId;
    selectedCreatureName_.clear();
    selectedCategory_ = SelectionCategory::DamageTaken;
    configTiming_ = TriggerTiming::OnDamageTaken;
    configCategory_ = TriggerCategory::Spell;
    configAnchor_ = TriggerAnchor::Target;
}
