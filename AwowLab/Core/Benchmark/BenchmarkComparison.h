#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Benchmark/MentorProfile.h"

namespace benchmark {

// Compares the loaded pull against a MentorProfile.
//
// The mentor is truncated to the point the user's pull ended (their death, or
// the wipe). Comparing a 3-minute attempt against an 8-minute kill would mostly
// report that the mentor lived longer.
//
// The Summary holds the headline numbers (duration, DPS delta, casts/min
// delta); the findings are the detail the replay timeline draws.
//
// Findings describe differences, not mistakes. Two players can diverge for
// reasons the log cannot see (group comp, mechanic targeting, assignments), so
// the wording stays descriptive.

// Headline numbers over the truncated window. Everything is a rate or a delta
// so it stays meaningful regardless of how long the attempt lasted.
struct BenchmarkSummary {
    int32_t comparedDurationMs = 0;  // How far the user got; mentor cut to match
    bool userDied = false;

    // True when the reference ran out before the player did. Both sides are
    // measured over the shorter window, and the report needs to say which
    // side that was.
    bool cappedByMentor = false;
    int32_t userDeathMs = -1;        // -1 when they survived to the end

    double userDps = 0.0;
    double mentorDps = 0.0;
    double dpsDelta = 0.0;           // user - mentor (negative = behind)

    double userCastsPerMinute = 0.0;
    double mentorCastsPerMinute = 0.0;
    double castsPerMinuteDelta = 0.0;

    int64_t userDamageTaken = 0;
    int64_t mentorDamageTaken = 0;

    // Where the damage difference actually came from. A player can match the
    // reference inside every burst window and still finish well behind on
    // filler alone, and the headline should be able to say which it was.
    double userBurstDps = 0.0;
    double mentorBurstDps = 0.0;
    double userFillerDps = 0.0;
    double mentorFillerDps = 0.0;

    // Estimated damage lost to each region over the compared window. Signed;
    // negative means behind the reference. These sum toward the overall
    // shortfall and answer "is my problem cooldowns or everything else".
    int64_t burstDamageDelta = 0;
    int64_t fillerDamageDelta = 0;
};

enum class FindingKind : uint8_t {
    // Cooldown findings are reported per nth-use, so a shifted-but-equal use
    // reads as a timing difference and NOT as a damage loss. The two are
    // separate lessons and get separate findings.
    CooldownDelayed,       // nth use went out later than the reference's nth
    CooldownWindowWeaker,  // nth use produced less while it was up
    CooldownUnused,        // Reference used it in this window; user never did
    CooldownFewerUses,     // Fewer total uses over the compared window

    // Filler throughput. Most of a pull is outside cooldowns, so this is
    // usually where the larger absolute damage loss lives even though each
    // individual mistake costs less than one inside a burst window.
    FillerDpsLower,     // Output between cooldowns trailed the reference
    FillerIdleHigher,   // More dead time between casts outside cooldowns

    CastRateLower,      // Materially fewer casts/min of a spell
    DamageTakenHigher,  // Took more from a specific ability
    TargetSelection,    // Damage split across npcs differed

    // These need the user's classifications to mean anything - knowing which
    // aura is a proc and which cast consumes it is spec knowledge the log
    // cannot supply on its own.
    ProcWasted,         // A proc expired without its consumer being cast
    MaintenanceUptime,  // A maintenance aura held at lower uptime
    ResourceCapped,     // Sat at max resource, wasting generation

    PhaseUncomparable,  // Boundary did not resolve in one of the two pulls
};

enum class FindingWeight : uint8_t {
    Info,       // Worth seeing, not necessarily wrong
    Notable,    // A real, sizeable divergence
    Major,      // Large enough that it likely changed the outcome
};

struct Finding {
    FindingKind kind = FindingKind::CooldownDelayed;
    FindingWeight weight = FindingWeight::Info;
    std::string phaseLabel;    // Empty for whole-pull findings
    uint32_t spellId = 0;      // 0 when not about one spell
    uint32_t npcId = 0;        // For target-selection findings

    // Where this happened, so the replay can jump straight to it. This is the
    // link between the summary and the timeline drill-down.
    int32_t pullOffsetMs = -1;

    // Pre-formatted, already localized by the caller. Descriptive, not
    // judgemental: "3.2s later than the reference", never "too slow".
    std::string summary;

    double userValue = 0.0;
    double mentorValue = 0.0;
};

struct PhaseComparison {
    std::string label;
    bool comparable = true;          // False when a boundary failed to resolve
    std::string uncomparableReason;  // Shown verbatim when comparable == false

    int32_t userDurationMs = 0;
    int32_t mentorDurationMs = 0;
    double userDps = 0.0;
    double mentorDps = 0.0;

    std::vector<Finding> findings;
};

struct BenchmarkResult {
    bool valid = false;
    // Why no comparison happened: spec mismatch, phase rules changed since the
    // profile was built, or spells still awaiting classification. Unconfirmed
    // classifications block rather than degrade - a wrong guess about what
    // counts as a cooldown would misreport every window built on it, and the
    // user would have no way to tell.
    std::string blockingReason;

    // Spells/auras the user still needs to rule on before this comparison can
    // run. The UI routes these straight into the classification review.
    std::vector<uint32_t> unclassifiedSpellIds;
    std::vector<uint32_t> unclassifiedAuraIds;

    BenchmarkSummary summary;
    std::vector<PhaseComparison> phases;
    std::vector<Finding> overall;   // Not tied to one phase

    size_t countAtLeast(FindingWeight w) const {
        size_t n = 0;
        for (const auto& p : phases)
            for (const auto& f : p.findings)
                if (f.weight >= w) ++n;
        for (const auto& f : overall)
            if (f.weight >= w) ++n;
        return n;
    }
};

// Thresholds below which a difference is noise rather than signal. Two pulls
// are never identical; without these the report drowns in trivia and stops
// being read.
struct BenchmarkThresholds {
    int32_t cooldownDelayMs = 5000;         // nth use went out this much later
    double cooldownWindowRatio = 0.85;      // nth window produced below this share

    // Filler is held to a tighter percentage than burst on purpose: it runs for
    // most of the pull, so a small shortfall there is a larger absolute loss
    // than a bigger shortfall inside a short window.
    double fillerDpsRatio = 0.92;           // Filler output below this share
    double fillerIdleShareDelta = 0.05;     // Idle time exceeding reference's by this

    double castRateDeltaPerMin = 2.0;       // Casts/min shortfall worth reporting
    double damageTakenRatio = 1.5;          // 50% more from one ability
    double targetSelectionShareDelta = 0.15; // Damage share to an npc differing by this
};

} // namespace benchmark
