#include "AutoAnnotationWizardState.h"
#include <algorithm>
#include <chrono>

AutoAnnotationWizardState::AutoAnnotationWizardState() {
    reset();
}

void AutoAnnotationWizardState::reset() {
    currentStep_ = WizardStep::TriggerSource;

    selectedCreatureGuid_.clear();
    selectedCreatureName_.clear();
    selectedSpellId_ = 0;
    selectedSpellName_.clear();
    spellCategory_ = WizardSpellCategory::Cast;
    actorNameFilter_.clear();

    timing_ = TriggerTiming::OnCastSuccess;
    trailIntervalSec_ = 1.0f;
    deathBehavior_ = ActorDeathBehavior::DisappearImmediately;

    anchor_ = TriggerAnchor::Target;
    offsetX_ = 0.0f;
    offsetY_ = 0.0f;
    leadTimeSec_ = 0.0f;

    shape_ = AnnotationShape::Circle;
    dimensions_ = {10.0f, 0.5f, 0.0f, 0.0f};
    pointCount_ = 6;

    color_ = {1.0f, 0.0f, 0.0f, 0.5f};
    visualStyle_ = AnnotationVisualStyle::Solid;
    stripeSpacing_ = 2.0f;
    rippleSpeed_ = 5.0f;
    rippleCount_ = 3.0f;
    motion_ = AnnotationMotionConfig{};
    travelTimeSec_ = 3.0f;

    durationType_ = 1;  // Timed
    durationSec_ = 30.0f;
    minStacks_ = 0;
    maxStacks_ = 0;
    stackColorCount_ = 0;
    stackThresholds_ = {0, 0, 0, 0};
    stackColors_ = {{
        {1.0f, 1.0f, 0.0f, 0.5f},
        {1.0f, 0.5f, 0.0f, 0.5f},
        {1.0f, 0.0f, 0.0f, 0.5f},
        {0.5f, 0.0f, 0.5f, 0.5f}
    }};
    isNegation_ = false;
    negatesSpellIds_.clear();
    pendingNegationSpellId_ = 0;
}

bool AutoAnnotationWizardState::canAdvance() const {
    return validateCurrentStep().empty();
}

bool AutoAnnotationWizardState::canGoBack() const {
    return currentStep_ != WizardStep::TriggerSource;
}

void AutoAnnotationWizardState::advance() {
    if (!canAdvance()) return;

    switch (currentStep_) {
        case WizardStep::TriggerSource:
            applyDefaultsForStep2();
            break;
        case WizardStep::EventType:
            applyDefaultsForStep3();
            break;
        case WizardStep::Positioning:
            applyDefaultsForStep4();
            break;
        default:
            break;
    }

    if (currentStep_ < WizardStep::DurationAdvanced) {
        currentStep_ = static_cast<WizardStep>(static_cast<int>(currentStep_) + 1);
    }
}

void AutoAnnotationWizardState::goBack() {
    if (!canGoBack()) return;
    currentStep_ = static_cast<WizardStep>(static_cast<int>(currentStep_) - 1);
}

bool AutoAnnotationWizardState::isComplete() const {
    return currentStep_ == WizardStep::DurationAdvanced && canAdvance();
}

bool AutoAnnotationWizardState::isAuraTiming() const {
    return ::isAuraTiming(timing_);
}

bool AutoAnnotationWizardState::isDebuffTiming() const {
    return ::isDebuffTiming(timing_);
}

bool AutoAnnotationWizardState::isBuffTiming() const {
    return ::isBuffTiming(timing_);
}

bool AutoAnnotationWizardState::usesFacingAnchor() const {
    return anchorUsesFacing(anchor_);
}

bool AutoAnnotationWizardState::isDuringTiming() const {
    return ::isDuringTiming(timing_);
}

std::string AutoAnnotationWizardState::validateCurrentStep() const {
    switch (currentStep_) {
        case WizardStep::TriggerSource: return validateStep1();
        case WizardStep::EventType: return validateStep2();
        case WizardStep::Positioning: return validateStep3();
        case WizardStep::ShapeSize: return validateStep4();
        case WizardStep::VisualStyle: return validateStep5();
        case WizardStep::DurationAdvanced: return validateStep6();
        case WizardStep::COUNT:
            return "Invalid wizard step";
    }
    return "";
}

std::string AutoAnnotationWizardState::validateStep1() const {
    if (spellCategory_ == WizardSpellCategory::Actor) {
        // Filled in from the tree selection.
        if (selectedCreatureName_.empty()) {
            return "Please select an actor";
        }
    } else {
        if (selectedSpellId_ == 0) {
            return "Please select a spell";
        }
    }
    return "";
}

std::string AutoAnnotationWizardState::validateStep2() const {
    if (isActorMode() && timing_ == TriggerTiming::ActorTrail) {
        if (trailIntervalSec_ < 0.5f || trailIntervalSec_ > 5.0f) {
            return "Trail interval must be 0.5-5.0 seconds";
        }
    }
    return "";
}

std::string AutoAnnotationWizardState::validateStep3() const {
    return "";
}

std::string AutoAnnotationWizardState::validateStep4() const {
    switch (shape_) {
        case AnnotationShape::Circle:
            if (dimensions_[0] <= 0) {
                return "Radius must be greater than 0";
            }
            break;
        case AnnotationShape::Donut:
            if (dimensions_[0] <= 0) {
                return "Outer radius must be greater than 0";
            }
            if (dimensions_[1] <= 0) {
                return "Inner radius must be greater than 0";
            }
            if (dimensions_[1] >= dimensions_[0]) {
                return "Inner radius must be less than outer radius";
            }
            break;
        case AnnotationShape::Rectangle:
            if (dimensions_[0] <= 0) {
                return "Width must be greater than 0";
            }
            if (dimensions_[1] <= 0) {
                return "Length must be greater than 0";
            }
            break;
        case AnnotationShape::Cone:
            if (dimensions_[0] <= 0) {
                return "Cone length must be greater than 0";
            }
            if (dimensions_[1] < 15.0f || dimensions_[1] > 180.0f) {
                return "Cone angle must be between 15 and 180 degrees";
            }
            break;
        case AnnotationShape::Line:
            if (dimensions_[0] <= 0) {
                return "Line length must be greater than 0";
            }
            if (dimensions_[1] <= 0) {
                return "Line width must be greater than 0";
            }
            break;
        case AnnotationShape::Star:
            if (dimensions_[0] <= 0) {
                return "Ray length must be greater than 0";
            }
            if (dimensions_[1] <= 0) {
                return "Ray width must be greater than 0";
            }
            if (pointCount_ < 3 || pointCount_ > 12) {
                return "Point count must be between 3 and 12";
            }
            break;
        default:
            break;
    }
    return "";
}

std::string AutoAnnotationWizardState::validateStep5() const {
    return "";
}

std::string AutoAnnotationWizardState::validateStep6() const {
    if (durationType_ == 1 && durationSec_ <= 0) {
        return "Duration must be greater than 0";
    }
    return "";
}

void AutoAnnotationWizardState::applyDefaultsForStep2() {
    switch (spellCategory_) {
        case WizardSpellCategory::Cast:
            timing_ = TriggerTiming::OnCastSuccess;
            break;
        case WizardSpellCategory::Damage:
            timing_ = TriggerTiming::OnDamage;
            break;
        case WizardSpellCategory::DamageTaken:
            timing_ = TriggerTiming::OnDamageTaken;
            break;
        case WizardSpellCategory::Debuff:
            timing_ = TriggerTiming::OnDebuffApplied;
            break;
        case WizardSpellCategory::Buff:
            timing_ = TriggerTiming::OnBuffApplied;
            break;
        case WizardSpellCategory::Actor:
            // Already chosen in the tree selection.
            break;
    }
}

void AutoAnnotationWizardState::applyDefaultsForStep3() {
    // Aura and damage-taken triggers anchor on the affected unit, so caster anchors are swapped out.
    if (isDebuffTiming()) {
        if (anchor_ == TriggerAnchor::Caster || anchor_ == TriggerAnchor::CasterFacing) {
            anchor_ = TriggerAnchor::Target;
        }
    } else if (isBuffTiming()) {
        if (anchor_ == TriggerAnchor::Caster || anchor_ == TriggerAnchor::CasterFacing) {
            anchor_ = TriggerAnchor::Target;
        }
    } else if (::isDamageTakenTiming(timing_)) {
        if (anchor_ == TriggerAnchor::Caster || anchor_ == TriggerAnchor::CasterFacing) {
            anchor_ = TriggerAnchor::Target;
        }
    }
}

void AutoAnnotationWizardState::applyDefaultsForStep4() {
    // Facing anchors only make sense for directional shapes.
    if (usesFacingAnchor()) {
        if (shape_ != AnnotationShape::Rectangle && shape_ != AnnotationShape::Cone) {
            shape_ = AnnotationShape::Cone;
            dimensions_ = {30.0f, 90.0f, 0.5f, 0.0f};  // 30yd, 90 deg
        }
    }
}

AutoAnnotationTrigger AutoAnnotationWizardState::buildTrigger() const {
    AutoAnnotationTrigger trigger;

    trigger.id = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

    trigger.category = isActorMode() ? TriggerCategory::Actor : TriggerCategory::Spell;

    if (!isActorMode()) {
        trigger.spell_id = selectedSpellId_;
        trigger.creature_guid = selectedCreatureGuid_;
        trigger.creature_name = selectedCreatureName_;
    }

    if (isActorMode()) {
        trigger.actorNameFilter = actorNameFilter_;
        trigger.trailIntervalMs = static_cast<int32_t>(trailIntervalSec_ * 1000.0f);
        trigger.deathBehavior = deathBehavior_;
    }

    trigger.timing = timing_;
    trigger.anchor = anchor_;

    trigger.shape = shape_;
    trigger.dimensions = dimensions_;
    // The wizard edits rotation in degrees; triggers store radians
    trigger.dimensions[3] = dimensions_[3] * kAnnotationDegToRad;
    trigger.pointCount = static_cast<uint8_t>(pointCount_);

    trigger.color = color_;
    trigger.visualStyle = visualStyle_;
    trigger.stripeSpacing = stripeSpacing_;
    trigger.rippleSpeed = rippleSpeed_;
    trigger.rippleCount = rippleCount_;

    trigger.motion = motion_;
    trigger.motion.travelTime_ms = static_cast<int32_t>(travelTimeSec_ * 1000.0f);

    trigger.durationType = (durationType_ == 0) ? AnnotationDuration::Encounter : AnnotationDuration::Timed;
    trigger.durationMs = static_cast<int32_t>(durationSec_ * 1000.0f);
    trigger.leadTimeMs = static_cast<int32_t>(leadTimeSec_ * 1000.0f);

    trigger.offsetX = offsetX_;
    trigger.offsetY = offsetY_;

    if (isAuraTiming()) {
        trigger.minStacks = static_cast<uint8_t>(minStacks_);
        trigger.maxStacks = static_cast<uint8_t>(maxStacks_);
        // Ctrl+click lets the slider take any typed value
        trigger.stackColorCount = static_cast<uint8_t>(
            std::clamp(stackColorCount_, 0, static_cast<int>(trigger.stackColors.size())));
        for (size_t i = 0; i < trigger.stackColors.size(); ++i) {
            trigger.stackColors[i].stackCount = static_cast<uint8_t>(stackThresholds_[i]);
            trigger.stackColors[i].color = stackColors_[i];
        }

        // getColorForStacks expects descending stackCount order.
        std::sort(trigger.stackColors.begin(),
                  trigger.stackColors.begin() + trigger.stackColorCount,
                  [](const auto& a, const auto& b) {
                      return a.stackCount > b.stackCount;
                  });
    }

    trigger.isNegation = isNegation_;
    trigger.negatesSpellIds = negatesSpellIds_;

    trigger.enabled = true;
    trigger.isAutoGenerated = true;

    return trigger;
}
