#pragma once

#include "AutoAnnotationTypes.h"
#include "AnnotationTypes.h"
#include "AnnotationMotionTypes.h"
#include <string>
#include <vector>
#include <cstdint>
#include <array>

enum class WizardStep : uint8_t {
    TriggerSource = 0,   // Step 1: Select spell or actor
    EventType,           // Step 2: Select timing mode
    Positioning,         // Step 3: Anchor and offsets
    ShapeSize,           // Step 4: Shape and dimensions
    VisualStyle,         // Step 5: Color, pattern, motion
    DurationAdvanced,    // Step 6: Duration, stacks, negation
    COUNT
};

// Category within spell source (or Actor for actor-based triggers)
enum class WizardSpellCategory : uint8_t {
    Cast = 0,
    Debuff,
    Buff,
    Damage,
    DamageTaken,  // Damage received by players (victim perspective)
    Actor         // Actor-based triggers (Follow, Trail, Spawn, Death)
};

// Intermediate data for the wizard flow, kept apart from the main panel state.
class AutoAnnotationWizardState {
public:
    AutoAnnotationWizardState();

    void reset();

    WizardStep getCurrentStep() const { return currentStep_; }
    bool canAdvance() const;
    bool canGoBack() const;
    void advance();
    void goBack();

    // On the final step and valid.
    bool isComplete() const;

    AutoAnnotationTrigger buildTrigger() const;

    // Empty string when valid, otherwise the message to show.
    std::string validateCurrentStep() const;

    bool isActorMode() const { return spellCategory_ == WizardSpellCategory::Actor; }
    bool isSpellMode() const { return spellCategory_ != WizardSpellCategory::Actor; }
    bool isAuraTiming() const;
    bool isDebuffTiming() const;
    bool isBuffTiming() const;
    bool usesFacingAnchor() const;
    bool isDuringTiming() const;

    // Step 1: trigger source (spell mode)
    std::string selectedCreatureGuid_;
    std::string selectedCreatureName_;
    uint32_t selectedSpellId_ = 0;
    std::string selectedSpellName_;
    WizardSpellCategory spellCategory_ = WizardSpellCategory::Cast;

    // Actor mode
    std::string actorNameFilter_;

    // Step 2: event type
    TriggerTiming timing_ = TriggerTiming::OnCastSuccess;

    // Actor mode extras
    float trailIntervalSec_ = 1.0f;
    ActorDeathBehavior deathBehavior_ = ActorDeathBehavior::DisappearImmediately;

    // Step 3: positioning
    TriggerAnchor anchor_ = TriggerAnchor::Target;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    float leadTimeSec_ = 0.0f;

    // Step 4: shape and size
    AnnotationShape shape_ = AnnotationShape::Circle;
    std::array<float, 4> dimensions_ = {10.0f, 0.5f, 0.0f, 0.0f};  // [3] is rotation in degrees
    int pointCount_ = 6;  // Star only

    // Step 5: visual style
    std::array<float, 4> color_ = {1.0f, 0.0f, 0.0f, 0.5f};
    AnnotationVisualStyle visualStyle_ = AnnotationVisualStyle::Solid;
    float stripeSpacing_ = 2.0f;
    float rippleSpeed_ = 5.0f;
    float rippleCount_ = 3.0f;

    AnnotationMotionConfig motion_;
    float travelTimeSec_ = 3.0f;  // edited in seconds, stored as motion_.travelTime_ms

    // Step 6: duration and advanced
    int durationType_ = 1;  // 0 = Encounter, 1 = Timed
    float durationSec_ = 30.0f;

    // Stack configuration (aura triggers only)
    int minStacks_ = 0;
    int maxStacks_ = 0;
    int stackColorCount_ = 0;
    std::array<int, 4> stackThresholds_ = {0, 0, 0, 0};
    std::array<std::array<float, 4>, 4> stackColors_ = {{
        {1.0f, 1.0f, 0.0f, 0.5f},
        {1.0f, 0.5f, 0.0f, 0.5f},
        {1.0f, 0.0f, 0.0f, 0.5f},
        {0.5f, 0.0f, 0.5f, 0.5f}
    }};

    bool isNegation_ = false;
    std::vector<uint32_t> negatesSpellIds_;

    // Pending negation spell input. Lives here rather than in a function-local static
    // so it resets with the wizard.
    uint32_t pendingNegationSpellId_ = 0;

private:
    WizardStep currentStep_ = WizardStep::TriggerSource;

    std::string validateStep1() const;
    std::string validateStep2() const;
    std::string validateStep3() const;
    std::string validateStep4() const;
    std::string validateStep5() const;
    std::string validateStep6() const;

    // Run when advancing into the named step.
    void applyDefaultsForStep2();
    void applyDefaultsForStep3();
    void applyDefaultsForStep4();
};
