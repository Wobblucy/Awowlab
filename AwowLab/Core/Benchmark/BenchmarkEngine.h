#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Benchmark/BenchmarkComparison.h"
#include "Core/Benchmark/MentorProfile.h"
#include "Core/Benchmark/SpellClassification.h"

namespace benchmark {

// Compares the user's pull against a reference profile.
//
// Both sides arrive as MentorProfile: the user's pull is extracted the same
// way the reference was, so both are measured by the same rules.
//
// The engine produces numbers and finding kinds, never text. Wording is left
// to the UI, which localizes it.

struct ComparisonRequest {
    const MentorProfile* user = nullptr;
    const MentorProfile* mentor = nullptr;

    // The user's confirmed classifications for this spec. Comparison does not
    // run while anything material is unconfirmed, since a wrong guess about
    // what counts as a cooldown would skew every window built on it.
    const SpecClassification* classification = nullptr;

    // When the user died, or -1 if they survived. The reference is truncated
    // here so both sides describe the same stretch of fight.
    int32_t userDeathMs = -1;

    BenchmarkThresholds thresholds;
};

// Reasons a comparison cannot run. Each is a distinct thing for the user to
// fix, so the UI can route them somewhere useful rather than showing one
// generic failure.
enum class BlockReason : uint8_t {
    None = 0,
    MissingProfile,        // One side was not supplied
    SpecMismatch,          // Different specs; the comparison would be noise
    EncounterMismatch,     // Different bosses
    PhaseRulesChanged,     // Rules differ from those the reference was cut with
    UnconfirmedSpells,     // Classifications still awaiting the user
    NoOverlap,             // The user died before anything comparable happened
};

BenchmarkResult compare(const ComparisonRequest& request);

// Exposed for the UI so a block can be explained before the user commits to
// importing anything.
BlockReason checkComparable(const ComparisonRequest& request);

} // namespace benchmark
