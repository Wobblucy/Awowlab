#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Benchmark/SpellClassification.h"

namespace benchmark {

// Turns one player's activity in one pull into classification suggestions,
// which the user then confirms or corrects.
//
// The heuristics read cadence (how often something was cast and how evenly
// spaced it was), the one signal available without per-spec knowledge. It
// separates long cooldowns from filler well, but cannot tell a defensive from
// an offensive cooldown or a proc consumer from a normal spender, which is why
// every suggestion needs confirming.

// One cast of one spell, reduced to what the heuristics actually read.
struct SuggesterCast {
    uint32_t spellId = 0;
    int32_t timestampMs = 0;
};

// One aura the player had on themselves, already resolved to an interval.
struct SuggesterAura {
    uint32_t spellId = 0;
    int32_t appliedMs = 0;
    int32_t removedMs = 0;
    bool selfApplied = false;   // Applied by the player to themselves
};

struct SuggesterInput {
    uint16_t specId = 0;
    std::string specName;
    std::string gameVersion;
    int32_t pullDurationMs = 0;

    std::vector<SuggesterCast> casts;
    std::vector<SuggesterAura> auras;

    // spellId -> display name, for the review UI. Missing names are fine;
    // the id is what everything else keys on.
    std::unordered_map<uint32_t, std::string> spellNames;

    // Real cooldown length per spell where the app knows one, from
    // SpellGCDDatabase. Cadence alone cannot distinguish "long cooldown" from
    // "situational spell the player only needed twice", and this resolves it.
    std::unordered_map<uint32_t, int32_t> cooldownMs;
};

// Build suggestions for one spec from one pull. Nothing is marked confirmed;
// the caller merges the result into ClassificationStore and sends the user to
// review whatever comes back pending.
SpecClassification suggestClassifications(const SuggesterInput& input);

} // namespace benchmark
