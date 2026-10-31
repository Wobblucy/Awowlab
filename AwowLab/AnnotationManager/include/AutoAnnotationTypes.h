#pragma once

#include "AnnotationTypes.h"
#include "AnnotationMotionTypes.h"
#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <algorithm>
#include <unordered_set>

enum class TriggerCategory : uint8_t {
    Spell = 0,    // Triggers on spell events (cast, damage, debuff)
    Actor         // Triggers based on actor presence/movement
};

enum class TriggerTiming : uint8_t {
    // Spell-based timings (category == Spell)
    OnCastSuccess = 0,  // SPELL_CAST_SUCCESS event (includes instant casts)
    OnCastStart,        // SPELL_CAST_START event (interruptible casts only)
    DuringCast,         // Spawn pools at caster position every X ms during cast (trail-like)
    OnDamage,           // SPELL_DAMAGE or SPELL_PERIODIC_DAMAGE event (from caster perspective)
    OnDamageTaken,      // SPELL_DAMAGE event (from victim perspective - players only)
    OnDebuffApplied,    // SPELL_AURA_APPLIED with aura_type == DEBUFF (one-time at apply)
    DuringDebuff,       // While debuff is active - annotation follows target, 250ms position sampling
    OnDebuffEnd,        // SPELL_AURA_REMOVED - snapshot position at debuff end
    Fixate,             // Shape from source NPC to target player while debuff active, 250ms sampling
    Tether,             // Like Fixate but source position is static (captured at debuff start)

    // Buff-based timings (category == Spell) - for enemy buffs
    OnBuffApplied,      // SPELL_AURA_APPLIED with aura_type == BUFF (one-time at apply)
    DuringBuff,         // While buff is active - annotation follows buffed unit, 250ms position sampling
    OnBuffEnd,          // SPELL_AURA_REMOVED for BUFF - snapshot position at buff end

    // Actor-based timings (category == Actor)
    ActorFollow,        // Create annotations at actor positions as timeline progresses (cached)
    ActorTrail,         // Leave persistent annotation every X seconds at actor position
    ActorSpawn,         // Single annotation when actor first appears
    ActorDeath          // Single annotation when actor dies
};

// What happens to actor-based annotations when the actor dies
enum class ActorDeathBehavior : uint8_t {
    DisappearImmediately = 0,  // Annotation vanishes when actor dies
    PersistAtDeathSpot,        // Annotation stays at death location (respects duration)
    FadeOverTime               // Annotation stays at its last spot and fades out over a few seconds
};

enum class TriggerAnchor : uint8_t {
    Caster = 0,         // Place annotation at source/caster position
    Target,             // Place annotation at destination/target position
    CasterFacing,       // Place at caster position, use caster's facing for rotation
    TargetFacing,       // Place at target position, use target's facing for rotation
    SourceToTarget      // Draw shape spanning from caster to target (beams, chains)
};

// Which aura sources a trigger matches.
enum class SourceFilter : uint8_t {
    Any = 0,
    SpecificCreature,   // Match only specific creature GUID (uses creature_guid field)
    EnvironmentOnly     // Match only environment sources (GUID "0000000000000000" or similar)
};

// Shortest spacing for trail and pool annotations. The trail loop steps by
// the interval, so zero or less would never finish; files and the UI are
// clamped to this.
inline constexpr int32_t kMinTrailIntervalMs = 100;

struct AutoAnnotationTrigger {
    uint64_t id = 0;
    TriggerCategory category = TriggerCategory::Spell;

    // Spell-based trigger fields (category == Spell)
    uint32_t spell_id = 0;
    std::string creature_guid;                    // Optional: limit to specific creature (empty = any hostile)
    std::string creature_name;                    // Creature name for display/grouping
    SourceFilter sourceFilter = SourceFilter::Any; // aura triggers only

    // Actor-based trigger fields (category == Actor)
    std::string actorNameFilter;                  // Match all actors with this display name
    int32_t trailIntervalMs = 1000;               // ActorTrail spacing
    ActorDeathBehavior deathBehavior = ActorDeathBehavior::DisappearImmediately;

    TriggerTiming timing = TriggerTiming::OnCastSuccess;
    TriggerAnchor anchor = TriggerAnchor::Target;

    AnnotationShape shape = AnnotationShape::Circle;
    std::array<float, 4> color = {1.0f, 0.0f, 0.0f, 0.5f};  // RGBA
    std::array<float, 4> dimensions = {10.0f, 0.5f, 0.0f, 0.0f};
    uint8_t pointCount = 6;                       // Number of rays for Star shape (3-12)

    // Visual style for uncertainty/wave indicators
    AnnotationVisualStyle visualStyle = AnnotationVisualStyle::Solid;
    float stripeSpacing = 2.0f;                   // Yards between stripes (for Striped/Hatched/RippleRings)
    float rippleSpeed = 5.0f;                     // Yards per second (for RippleRings)
    float rippleCount = 3.0f;                     // Number of visible ring cycles (for RippleRings)

    // Edge effects
    bool enableEdgeFade = false;                  // Soft alpha fade at edges
    bool enableGlow = false;                      // Brightness glow at edges
    bool enableOutline = false;                   // Colored border around shape
    bool enableGradient = false;                  // Color gradient center-to-edge
    float edgeFadeWidth = 0.15f;                  // Normalized fade width (0.0-0.3)
    float outlineWidth = 0.05f;                   // Normalized outline width (0.0-0.15)
    float glowIntensity = 1.0f;                   // Glow strength (0.0-2.0)

    // Dual color support
    bool useSecondaryColor = false;               // Use secondary color for gradients/outlines/patterns
    bool useInnerZoneColor = false;               // Use inner zone color for center region
    std::array<float, 4> secondaryColor = {0.5f, 0.5f, 0.5f, 1.0f};  // Gray default
    std::array<float, 4> innerZoneColor = {0.0f, 1.0f, 0.0f, 0.5f};  // Green default (safe zone)

    AnnotationDuration durationType = AnnotationDuration::Timed;
    int32_t durationMs = 30000;
    int32_t leadTimeMs = 0;                       // Fire annotation this many ms BEFORE the event (0 = at event time)
    int32_t castExtendMs = 0;                     // For DuringCast: continue spawning pools this many ms AFTER cast ends

    // Position offset from anchor (world coordinates, in yards)
    float offsetX = 0.0f;                         // Positive = East, Negative = West
    float offsetY = 0.0f;                         // Positive = North, Negative = South

    // Stack/dose filtering (for buff/debuff triggers only)
    uint8_t minStacks = 0;                        // Minimum stacks to show annotation (0 = no minimum)
    uint8_t maxStacks = 0;                        // Maximum stacks to show annotation (0 = no maximum)

    // Stack-based color thresholds (up to 4 thresholds)
    // When stacks >= threshold, use that color. Evaluated in descending order.
    // Example: [{3, red}, {2, yellow}, {1, green}] means 3+ red, 2 yellow, 1 green
    struct StackColorThreshold {
        uint8_t stackCount = 0;                   // Stacks required for this color
        std::array<float, 4> color = {1.0f, 0.0f, 0.0f, 0.5f};  // RGBA
    };
    std::array<StackColorThreshold, 4> stackColors = {};  // Up to 4 color thresholds
    uint8_t stackColorCount = 0;                  // Number of active stack color thresholds (0 = use base color)

    AnnotationMotionConfig motion;

    bool enabled = true;
    bool isAutoGenerated = true;                  // Marks annotations created by triggers
    bool ignoreEarlyPullCasts = false;            // Skip casts in first 500ms of encounter (filters fake 0:00 casts)

    // Negation configuration - this trigger clears other annotations when it fires
    bool isNegation = false;                      // Acts as a negation zone
    std::vector<uint32_t> negatesSpellIds;        // Spell IDs to negate (empty = none)

    std::string getDisplayName(const std::string& spellName) const {
        std::string result;

        if (category == TriggerCategory::Actor) {
            result = actorNameFilter.empty() ? "(All Actors)" : actorNameFilter;
            result += " -> ";
            switch (timing) {
                case TriggerTiming::ActorFollow: result += "Follow"; break;
                case TriggerTiming::ActorTrail: result += "Trail"; break;
                case TriggerTiming::ActorSpawn: result += "Spawn"; break;
                case TriggerTiming::ActorDeath: result += "Death"; break;
                default: result += "Actor"; break;
            }
        } else {
            result = spellName.empty() ? std::to_string(spell_id) : spellName;
            result += " -> ";
            switch (shape) {
                case AnnotationShape::Circle: result += "Circle"; break;
                case AnnotationShape::Rectangle: result += "Rect"; break;
                case AnnotationShape::Cone: result += "Cone"; break;
                case AnnotationShape::Line: result += "Line"; break;
                case AnnotationShape::Star: result += "Star"; break;
                case AnnotationShape::Donut: result += "Donut"; break;
                default: result += "Shape"; break;
            }
            result += " @ ";
            switch (anchor) {
                case TriggerAnchor::Caster: result += "Caster"; break;
                case TriggerAnchor::Target: result += "Target"; break;
                case TriggerAnchor::CasterFacing: result += "Caster (Facing)"; break;
                case TriggerAnchor::TargetFacing: result += "Target (Facing)"; break;
                case TriggerAnchor::SourceToTarget: result += "Caster to Target"; break;
            }
        }
        return result;
    }

    bool isActorBased() const {
        return category == TriggerCategory::Actor;
    }

    static bool isActorTiming(TriggerTiming t) {
        return t == TriggerTiming::ActorFollow ||
               t == TriggerTiming::ActorTrail ||
               t == TriggerTiming::ActorSpawn ||
               t == TriggerTiming::ActorDeath;
    }

    bool isAuraTiming() const {
        return timing == TriggerTiming::OnDebuffApplied ||
               timing == TriggerTiming::DuringDebuff ||
               timing == TriggerTiming::OnDebuffEnd ||
               timing == TriggerTiming::Fixate ||
               timing == TriggerTiming::Tether ||
               timing == TriggerTiming::OnBuffApplied ||
               timing == TriggerTiming::DuringBuff ||
               timing == TriggerTiming::OnBuffEnd;
    }

    bool passesStackFilter(uint8_t stacks) const {
        if (minStacks > 0 && stacks < minStacks) return false;
        if (maxStacks > 0 && stacks > maxStacks) return false;
        return true;
    }

    // Falls back to the base color when no threshold matches.
    std::array<float, 4> getColorForStacks(uint8_t stacks) const {
        if (stackColorCount == 0) {
            return color;
        }

        // stackColors is kept in descending stackCount order, so the first match is the highest.
        for (uint8_t i = 0; i < stackColorCount; ++i) {
            if (stacks >= stackColors[i].stackCount) {
                return stackColors[i].color;
            }
        }

        return color;
    }
};

// Used for UI display - groups spells by hostile creature
struct HostileCreatureSpells {
    std::string creature_guid;        // GUID (empty string for "Environment")
    std::string creature_name;        // Display name (e.g., "Sikran", "Environment")
    std::vector<uint32_t> spell_ids;  // Unique spell IDs cast by this creature (from SpellCastDatabase)
    std::vector<uint32_t> debuff_ids; // Unique debuff spell IDs applied by this creature (from AuraDatabase)
    std::vector<uint32_t> damage_ids; // Unique damage spell IDs dealt by this creature (from CombatDatabase)
    std::vector<uint32_t> buff_ids;   // Unique buff spell IDs applied by this creature (from AuraDatabase)
    std::unordered_set<uint32_t> interruptible_ids;  // Spells with cast bar (start_timestamp_ms > 0)

    void sortSpells() {
        std::sort(spell_ids.begin(), spell_ids.end());
        std::sort(debuff_ids.begin(), debuff_ids.end());
        std::sort(damage_ids.begin(), damage_ids.end());
        std::sort(buff_ids.begin(), buff_ids.end());
    }

    bool isInterruptible(uint32_t spellId) const {
        return interruptible_ids.count(spellId) > 0;
    }
};

inline const char* triggerCategoryToString(TriggerCategory category) {
    switch (category) {
        case TriggerCategory::Spell: return "Spell";
        case TriggerCategory::Actor: return "Actor";
        default: return "Unknown";
    }
}

inline const char* triggerTimingToString(TriggerTiming timing) {
    switch (timing) {
        case TriggerTiming::OnCastSuccess: return "On Cast Success";
        case TriggerTiming::OnCastStart: return "On Cast Start";
        case TriggerTiming::DuringCast: return "During Cast";
        case TriggerTiming::OnDamage: return "On Damage";
        case TriggerTiming::OnDamageTaken: return "On Damage Taken";
        case TriggerTiming::OnDebuffApplied: return "On Debuff Applied";
        case TriggerTiming::DuringDebuff: return "During Debuff";
        case TriggerTiming::OnDebuffEnd: return "On Debuff End";
        case TriggerTiming::Fixate: return "Fixate";
        case TriggerTiming::Tether: return "Tether";
        case TriggerTiming::OnBuffApplied: return "On Buff Applied";
        case TriggerTiming::DuringBuff: return "During Buff";
        case TriggerTiming::OnBuffEnd: return "On Buff End";
        case TriggerTiming::ActorFollow: return "Follow Actor";
        case TriggerTiming::ActorTrail: return "Leave Trail";
        case TriggerTiming::ActorSpawn: return "On Spawn";
        case TriggerTiming::ActorDeath: return "On Death";
        default: return "Unknown";
    }
}

inline const char* actorDeathBehaviorToString(ActorDeathBehavior behavior) {
    switch (behavior) {
        case ActorDeathBehavior::DisappearImmediately: return "Disappear Immediately";
        case ActorDeathBehavior::PersistAtDeathSpot: return "Persist at Death Spot";
        case ActorDeathBehavior::FadeOverTime: return "Fade Over Time";
        default: return "Unknown";
    }
}

inline const char* triggerAnchorToString(TriggerAnchor anchor) {
    switch (anchor) {
        case TriggerAnchor::Caster: return "At Caster";
        case TriggerAnchor::Target: return "At Target";
        case TriggerAnchor::CasterFacing: return "At Caster (Facing)";
        case TriggerAnchor::TargetFacing: return "At Target (Facing)";
        case TriggerAnchor::SourceToTarget: return "Source -> Target";
        default: return "Unknown";
    }
}

inline const char* sourceFilterToString(SourceFilter filter) {
    switch (filter) {
        case SourceFilter::Any: return "Any Source";
        case SourceFilter::SpecificCreature: return "Specific Creature";
        case SourceFilter::EnvironmentOnly: return "Environment Only";
        default: return "Unknown";
    }
}

inline bool anchorUsesFacing(TriggerAnchor anchor) {
    return anchor == TriggerAnchor::CasterFacing || anchor == TriggerAnchor::TargetFacing;
}

// Position source, ignoring facing.
inline bool anchorAtCaster(TriggerAnchor anchor) {
    return anchor == TriggerAnchor::Caster || anchor == TriggerAnchor::CasterFacing;
}

inline bool anchorSpansSourceToTarget(TriggerAnchor anchor) {
    return anchor == TriggerAnchor::SourceToTarget;
}

// Shared by AutoAnnotationTrigger and AutoAnnotationWizardState.
inline bool isActorTiming(TriggerTiming timing) {
    return timing == TriggerTiming::ActorFollow ||
           timing == TriggerTiming::ActorTrail ||
           timing == TriggerTiming::ActorSpawn ||
           timing == TriggerTiming::ActorDeath;
}

inline bool isAuraTiming(TriggerTiming timing) {
    return timing == TriggerTiming::OnDebuffApplied ||
           timing == TriggerTiming::DuringDebuff ||
           timing == TriggerTiming::OnDebuffEnd ||
           timing == TriggerTiming::Fixate ||
           timing == TriggerTiming::Tether ||
           timing == TriggerTiming::OnBuffApplied ||
           timing == TriggerTiming::DuringBuff ||
           timing == TriggerTiming::OnBuffEnd;
}

inline bool isDebuffTiming(TriggerTiming timing) {
    return timing == TriggerTiming::OnDebuffApplied ||
           timing == TriggerTiming::DuringDebuff ||
           timing == TriggerTiming::OnDebuffEnd ||
           timing == TriggerTiming::Fixate ||
           timing == TriggerTiming::Tether;
}

inline bool isBuffTiming(TriggerTiming timing) {
    return timing == TriggerTiming::OnBuffApplied ||
           timing == TriggerTiming::DuringBuff ||
           timing == TriggerTiming::OnBuffEnd;
}

// "During" timings follow the target while the aura is up.
inline bool isDuringTiming(TriggerTiming timing) {
    return timing == TriggerTiming::DuringDebuff ||
           timing == TriggerTiming::DuringBuff;
}

inline bool isDamageTakenTiming(TriggerTiming timing) {
    return timing == TriggerTiming::OnDamageTaken;
}
