#pragma once

#include "AutoAnnotationTypes.h"
#include "AnnotationTypes.h"
#include "AnnotationMotionTypes.h"
#include "UI/ISpellIconRenderer.h"
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

class AutoAnnotationTriggerManager;
class AnnotationManager;
class SpellCastDatabase;
class CombatDatabase;
class AuraDatabase;
class EventDatabase;
class UIAutoAnnotationWizard;

// Lists hostile creatures and their spells, and creates and edits auto-annotation triggers.
class UIAutoAnnotationPanel {
public:
    // Defined in the .cpp so unique_ptr can see the complete wizard type.
    UIAutoAnnotationPanel();
    ~UIAutoAnnotationPanel();

    void setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& data);
    void setTriggerManager(AutoAnnotationTriggerManager* manager);
    void setSpellIconLoader(awow::ISpellIconRenderer* loader) { iconLoader_ = loader; }

    // Needed for the preview.
    void setAnnotationManager(AnnotationManager* manager) { annotationManager_ = manager; }
    void setDatabases(const SpellCastDatabase* casts, const CombatDatabase* combat,
                      const AuraDatabase* auras, const EventDatabase* events);

    // True if anything changed.
    bool render(int32_t currentTimestamp_ms);

    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

    // Used by the context menu to jump to a spell's entry.
    void navigateToSpell(uint32_t spellId);

    // Picks the trigger timing.
    enum class SelectionCategory { Cast, Debuff, Buff, Damage, DamageTaken, Actor };

    // The list a saved trigger is reopened under for editing.
    static SelectionCategory selectionCategoryFor(TriggerCategory category, TriggerTiming timing) {
        if (category == TriggerCategory::Actor) {
            return SelectionCategory::Actor;
        }
        switch (timing) {
            case TriggerTiming::OnDebuffApplied:
            case TriggerTiming::DuringDebuff:
            case TriggerTiming::OnDebuffEnd:
            case TriggerTiming::Fixate:
            case TriggerTiming::Tether:
                return SelectionCategory::Debuff;
            case TriggerTiming::OnBuffApplied:
            case TriggerTiming::DuringBuff:
            case TriggerTiming::OnBuffEnd:
                return SelectionCategory::Buff;
            case TriggerTiming::OnDamage:
                return SelectionCategory::Damage;
            case TriggerTiming::OnDamageTaken:
                return SelectionCategory::DamageTaken;
            default:
                return SelectionCategory::Cast;
        }
    }

private:
    bool visible_ = false;
    std::vector<HostileCreatureSpells> hostileSpells_;
    AutoAnnotationTriggerManager* triggerManager_ = nullptr;
    awow::ISpellIconRenderer* iconLoader_ = nullptr;

    // Preview support
    AnnotationManager* annotationManager_ = nullptr;
    const SpellCastDatabase* spellCastDb_ = nullptr;
    const CombatDatabase* combatDb_ = nullptr;
    const AuraDatabase* auraDb_ = nullptr;
    const EventDatabase* eventDb_ = nullptr;
    bool showPreview_ = false;

    // Grouped by creature name, not GUID.
    std::string selectedCreatureName_;
    uint32_t selectedSpellId_ = 0;

    SelectionCategory selectedCategory_ = SelectionCategory::Cast;

    uint64_t editingTriggerId_ = 0;  // 0 = creating a new trigger

    // Trigger form state
    TriggerCategory configCategory_ = TriggerCategory::Spell;
    TriggerTiming configTiming_ = TriggerTiming::OnCastSuccess;
    TriggerAnchor configAnchor_ = TriggerAnchor::Target;
    AnnotationShape configShape_ = AnnotationShape::Circle;
    float configColor_[4] = {1.0f, 0.0f, 0.0f, 0.5f};
    float configDimensions_[4] = {10.0f, 0.5f, 0.0f, 0.0f};
    int configPointCount_ = 6;  // Number of rays for Star shape (3-12)
    AnnotationVisualStyle configVisualStyle_ = AnnotationVisualStyle::Solid;
    float configStripeSpacing_ = 2.0f;     // Yards between stripes
    float configRippleSpeed_ = 5.0f;       // Yards per second for RippleRings
    float configRippleCount_ = 3.0f;       // Number of visible ring cycles

    // Edge effects
    bool configEnableEdgeFade_ = false;
    bool configEnableGlow_ = false;
    bool configEnableOutline_ = false;
    bool configEnableGradient_ = false;
    float configEdgeFadeWidth_ = 0.15f;
    float configOutlineWidth_ = 0.05f;
    float configGlowIntensity_ = 1.0f;

    // Dual color support
    bool configUseSecondaryColor_ = false;
    bool configUseInnerZoneColor_ = false;
    float configSecondaryColor_[4] = {0.5f, 0.5f, 0.5f, 1.0f};  // Gray default
    float configInnerZoneColor_[4] = {0.0f, 1.0f, 0.0f, 0.5f};  // Green default (safe zone)

    int configDurationType_ = 0;  // 0 = Encounter, 1 = Timed
    float configDurationSec_ = 30.0f;
    float configLeadTimeSec_ = 0.0f;  // Fire annotation this many seconds before the event
    bool configIgnoreEarlyPull_ = false;  // Skip casts in first 500ms of encounter
    float configOffsetX_ = 0.0f;  // X offset from anchor position (yards)
    float configOffsetY_ = 0.0f;  // Y offset from anchor position (yards)

    // Actor triggers
    std::string configActorNameFilter_;
    float configTrailIntervalSec_ = 1.0f;
    float configCastExtendSec_ = 0.0f;           // DuringCast: extend pool spawning after cast ends
    ActorDeathBehavior configDeathBehavior_ = ActorDeathBehavior::DisappearImmediately;

    // Aura triggers only
    static constexpr int kMaxStackColors = 4;     // size of AutoAnnotationTrigger::stackColors
    SourceFilter configSourceFilter_ = SourceFilter::Any;
    int configMinStacks_ = 0;                     // Minimum stacks to show (0 = no min)
    int configMaxStacks_ = 0;                     // Maximum stacks to show (0 = no max)
    int configStackColorCount_ = 0;
    int configStackThresholds_[kMaxStackColors] = {0, 0, 0, 0};
    float configStackColors_[kMaxStackColors][4] = {
        {1.0f, 1.0f, 0.0f, 0.5f},  // yellow
        {1.0f, 0.5f, 0.0f, 0.5f},  // orange
        {1.0f, 0.0f, 0.0f, 0.5f},  // red
        {0.5f, 0.0f, 0.5f, 0.5f}   // purple
    };

    AnnotationMotionConfig configMotion_;
    float configTravelTimeSec_ = 3.0f;            // edited in seconds, stored in ms

    bool configIsNegation_ = false;
    std::vector<uint32_t> configNegatesSpellIds_;
    uint32_t configNewNegateSpellId_ = 0;

    void loadTriggerForEditing(const AutoAnnotationTrigger& trigger);
    std::string getSpellName(uint32_t spellId) const;

    void renderCreatureList();
    void renderTriggerConfig();
    void renderActiveTriggers();

    // Sections of the trigger form, top to bottom
    void renderConfigHeader();
    bool timingCombo(const char* label, const TriggerTiming* choices, const char* const* names, int count);
    void renderActorTimingControls();
    void renderDebuffTimingControls();
    void renderBuffTimingControls();
    void renderCastTimingControls();
    void renderSourceFilterCombo(const char* anyTip, const char* creatureTip, const char* environmentTip);
    void renderLeadTimeControls();
    void renderAnchorControls();
    void renderShapeControls();
    void renderStackControls();
    void renderVisualStyleControls();
    void renderMotionControls();
    void renderProjectileControls();
    void renderNegationControls();

    AutoAnnotationTrigger buildTriggerFromForm() const;
    void submitForm();
    void resetForm();

    void updatePreviewAnnotation();
    void clearPreviewIfNeeded();

    bool wizardOpen_ = false;
    std::unique_ptr<UIAutoAnnotationWizard> wizard_;
    void openWizard();
    void handleWizardResult();
};
