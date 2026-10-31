#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ExtractedData;

namespace awow::analysis {

// Which spell ids a player actually pressed.
//
// One ability arrives under several ids: the press, a proc that casts it, a
// per-target trigger, and the glaives an animation throws. Counting the rows
// inflates a Havoc player's activity roughly threefold - Throw Glaive alone
// contributes three ids at about fifty a minute between them - so anything
// derived from cast counts is wrong until the presses are known.
//
// This cannot be inferred. Three rules were measured against a 185 minute
// sample and each failed on a different ability: median cast gap admits passive
// soul generation; "does it ever appear alone" rejects Death Sweep and Blade
// Dance, which stand alone in 1-2% of casts because they always throw a glaive;
// and power cost is silent on the free abilities. Death Sweep and a pure
// trigger are statistically identical on timing alone.
//
// So the player is asked. Casts are clustered by time, the obvious non-presses
// are excluded first, and what remains is a short list of groups - about four
// for Havoc - where the question "which of these did you press?" is one a
// player can answer from their own keybinds.

// Casts landing within this of each other are one action. Measured: at 100 ms
// the Throw Glaive ids look independent (61% and 23% alone) and the grouping
// misses the relationship entirely; at 400 ms they are 7% and 0%, and the
// clusters are complete without swallowing unrelated presses.
constexpr int32_t kClusterWindowMs = 400;

// Above this many casts a minute an ability cannot be a keypress: the global
// cooldown is about 1.5 s, so roughly 40 presses a minute is the ceiling even
// ignoring cast time, and anything at triple that is generated. Used to
// pre-exclude rather than to decide - the user still sees what was excluded.
constexpr double kMaxPressesPerMinute = 15.0;

// How many times two ids must land together before the pairing is treated as a
// relationship rather than a coincidence. Two abilities used once a minute each
// will collide occasionally by chance in a five minute pull; a real pairing -
// the glaives that always accompany a Blade Dance - matches far more often than
// that. Ten is the floor for a five minute encounter.
constexpr uint32_t kMinPairMatches = 10;

struct SpellOption {
    uint32_t spellId = 0;
    std::string name;
    uint32_t casts = 0;
    double perMinute = 0.0;

    // How often this id appears without any other id beside it. Reported so the
    // user can see why a group was formed, not used to decide anything.
    double aloneFraction = 0.0;

    // Set when the frequency rule already ruled it out.
    bool excludedByRate = false;
};

// Ids that keep arriving together, offered as one question.
struct PressGroup {
    std::vector<SpellOption> options;

    // Filled in by the caller once the player answers. Empty means unanswered;
    // a group may legitimately resolve to nothing pressed, which is the right
    // answer for a cluster that is all triggers.
    std::vector<uint32_t> pressed;

    // True when every option was ruled out by rate, so the question is a
    // confirmation rather than a choice.
    [[nodiscard]] bool allExcluded() const;
};

// Group one player's casts across however many fights are in hand.
//
// Pass every fight of the baseline: a single pull is too short to see which ids
// travel together, and the answer is meant to hold for the spec rather than for
// one fight.
struct FightCasts {
    const ExtractedData* data = nullptr;
    std::string playerGuid;
    int32_t durationMs = 0;
};

[[nodiscard]] std::vector<PressGroup> groupCasts(const std::vector<FightCasts>& fights);

// The answers, stored per spec rather than per player: which id is the press is
// a fact about the spec, so one player answering it serves everyone. Kept in
// data/ so a shipped default can be corrected rather than re-answered.
struct PressProfile {
    uint16_t specId = 0;
    std::string specName;
    std::string gameBuild;          // ids move between patches
    std::vector<uint32_t> pressed;

    // Presses that do not occupy the shared global cooldown: Blur, Disrupt,
    // racials, trinkets, and abilities on their own global such as Vengeful
    // Retreat, which shares one with Fel Rush rather than with the rotation.
    //
    // Kept apart because they are real decisions but not rotational ones.
    // Counting them as activity fills gaps that were genuine downtime, so a
    // player weaving defensives through a movement phase looks busy; leaving
    // them out of the press list entirely would instead lose the fact that
    // they were used at all.
    //
    // Not inferable from a log - nothing distinguishes an off-global cast from
    // a fast one - so it is asked alongside the press question.
    std::vector<uint32_t> offGlobal;

    [[nodiscard]] bool isPress(uint32_t spellId) const;
    [[nodiscard]] bool isOffGlobal(uint32_t spellId) const;
    [[nodiscard]] bool empty() const { return pressed.empty(); }
};

[[nodiscard]] std::string writePressProfile(const PressProfile& profile,
                                            const std::string& path);
[[nodiscard]] bool readPressProfile(const std::string& path, PressProfile& out);

} // namespace awow::analysis
