#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ExtractedData;

namespace awow::analysis {

struct PressProfile;

// What good play looks like for one spec and hero tree, reduced to bands.
//
// A single pull says nothing on its own. "18.8% of Fury overcapped" reads as a
// fault until you know the best players in the same content waste 15-20%, and
// "8.4% downtime" reads as a fault until you know the median is 17%. Both of
// those were reported as mistakes during development before the bands existed.
//
// So a baseline is not shipped as fights. It is reduced to this - a few dozen
// numbers - and travels with the pull being reviewed. The agent reading the
// package compares two small things instead of deriving norms from 27 fights of
// raw events.

// One measure, as p25 / median / p75 across the sampled fights, plus the range
// so a reader can see whether the band is tight enough to mean anything. Eye
// Beam at 2.89-3.08 is diagnostic; Essence Break uptime at 19-36 is not.
struct Band {
    std::string measure;
    double p25 = 0.0;
    double median = 0.0;
    double p75 = 0.0;
    double best = 0.0;
    double worst = 0.0;
    uint32_t samples = 0;

    // How wide the band is relative to its middle. A reader should treat
    // anything over about 0.5 as weak evidence either way.
    [[nodiscard]] double spread() const;
};

// A mistake worth reporting, with how often the sampled players make it.
//
// Prevalence is the point. "You munched 8 of 9 Hunt glaives" is alarming until
// you know the best players munch 60% of theirs, at which point it is a polish
// item rather than the headline. Without this a review promotes common,
// low-value mistakes over rare, costly ones.
struct MistakeBand {
    std::string key;                // "hunt_munch"
    std::string description;        // shown to the reader
    double p25 = 0.0;
    double median = 0.0;
    double p75 = 0.0;
    double best = 0.0;              // the cleanest single pull managed this
    uint32_t samples = 0;

    // Left empty when a damage cost could not be demonstrated, which is the
    // honest answer more often than not: for Hunt munching, the players who
    // munched more did not reliably parse lower.
    std::string costNote;
};

struct Expectation {
    uint16_t specId = 0;
    std::string specName;
    std::string heroTree;
    std::string difficulty;         // what the sample was drawn from
    std::string gameBuild;
    uint32_t fights = 0;
    uint32_t players = 0;
    double minutes = 0.0;

    std::vector<Band> bands;
    std::vector<MistakeBand> mistakes;

    [[nodiscard]] const Band* band(const std::string& measure) const;
};

// One fight's contribution, the same shape the package builder already holds.
struct ExpectationFight {
    const ExtractedData* data = nullptr;
    std::string playerGuid;
    std::string playerName;
    int32_t durationMs = 0;
};

// Reduce a baseline to bands.
//
// The press profile decides what counts as an action: without it every proc and
// animation trigger is counted as a decision, which inflates a Havoc player's
// activity roughly threefold and makes every rate meaningless. Passing an empty
// profile is allowed and produces bands over raw cast rows, clearly labelled as
// such, since that is better than silently pretending the numbers are presses.
[[nodiscard]] Expectation buildExpectation(const std::vector<ExpectationFight>& fights,
                                           const PressProfile& presses);

[[nodiscard]] std::string writeExpectation(const Expectation& expectation,
                                           const std::string& path);
[[nodiscard]] bool readExpectation(const std::string& path, Expectation& out);

} // namespace awow::analysis
