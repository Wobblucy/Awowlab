#pragma once

#include "AutoAnnotationWizardState.h"
#include "UI/ISpellIconRenderer.h"
#include <vector>
#include <memory>

class AutoAnnotationTriggerManager;
class AnnotationManager;
class SpellCastDatabase;
class CombatDatabase;
class AuraDatabase;
class EventDatabase;
struct HostileCreatureSpells;

enum class WizardResult {
    InProgress,
    Completed,
    Cancelled
};

// Modal wizard for creating auto-annotation triggers.
class UIAutoAnnotationWizard {
public:
    UIAutoAnnotationWizard();
    ~UIAutoAnnotationWizard();

    // Call before open().
    void setDependencies(
        AutoAnnotationTriggerManager* triggerManager,
        AnnotationManager* annotationManager,
        const SpellCastDatabase* spellCasts,
        const CombatDatabase* combat,
        const AuraDatabase* auras,
        const EventDatabase* events,
        awow::ISpellIconRenderer* iconLoader,
        const std::vector<HostileCreatureSpells>* hostileSpells
    );

    // Also resets the state.
    void open();

    void reset();

    WizardResult render(int32_t currentTimestamp_ms);

    // Read the result from here after completion.
    const AutoAnnotationWizardState& getState() const { return state_; }
    AutoAnnotationWizardState& getState() { return state_; }

    bool isOpen() const { return isOpen_; }

private:
    bool isOpen_ = false;
    AutoAnnotationWizardState state_;

    // Not owned
    AutoAnnotationTriggerManager* triggerManager_ = nullptr;
    AnnotationManager* annotationManager_ = nullptr;
    const SpellCastDatabase* spellCastDb_ = nullptr;
    const CombatDatabase* combatDb_ = nullptr;
    const AuraDatabase* auraDb_ = nullptr;
    const EventDatabase* eventDb_ = nullptr;
    awow::ISpellIconRenderer* iconLoader_ = nullptr;
    const std::vector<HostileCreatureSpells>* hostileSpells_ = nullptr;

    // In UIAutoAnnotationWizard_Steps.cpp
    void renderStep1_TriggerSource();
    void renderStep2_EventType();
    void renderStep3_Positioning();
    void renderStep4_ShapeSize();
    void renderStep5_VisualStyle();
    void renderStep6_DurationAdvanced();

    void renderStepIndicator();
    WizardResult renderNavigationButtons();
    void renderValidationError();

    std::string getSpellName(uint32_t spellId) const;

    void renderCreatureSpellTree();

    // One selectable spell with its icon. Selecting it sets the spell and
    // category; returns true on that click so the caller can set the source.
    bool renderSpellRow(uint32_t spellId, WizardSpellCategory category, int imguiIdOffset);

    // Spells grouped by what players were hit by, rather than by caster.
    void renderDamageTakenSpellList();

    void renderShapeSelector();
    void setDefaultDimensions(AnnotationShape shape);
};
