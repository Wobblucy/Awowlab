#include "Core/Benchmark/ClassificationSuggester.h"

#include <algorithm>
#include <cmath>

namespace benchmark {

namespace {

// Cadence bands, in casts per minute. The gaps between them are deliberate:
// a spell landing between two bands gets a lower confidence rather than being
// forced into whichever is nearer.
constexpr float kMajorCooldownMaxCpm = 1.0f;   // Sub-1/min: two-minute-ish
constexpr float kMinorCooldownMaxCpm = 3.0f;   // 1-3/min: short cooldown
constexpr float kFillerMinCpm        = 6.0f;   // 6+/min: pressed constantly

// Below this many casts, cadence says nothing useful - two casts have exactly
// one gap, which is not a rhythm.
constexpr int kMinCastsForCadence = 3;

// How much the gaps between casts may vary before the spell reads as
// situational rather than rhythmic. Expressed as a share of the median gap.
constexpr double kRegularGapTolerance = 0.35;

// A cooldown this long is a defining ability of the rotation rather than
// something folded into it, whatever its cast rate happens to be.
constexpr int32_t kMajorCooldownMs = 90000;

// Auras this short are almost never something the player maintains; they are
// the tail of a proc or a brief buff riding a cooldown.
constexpr int32_t kMaintenanceMinDurationMs = 8000;

// A cooldown is up for part of a pull, not most of it. Anything above this is
// a passive or a stacking resource aura wearing a long duration, and calling
// it a cooldown would make the whole pull read as one burst window.
constexpr float kMaxCooldownUptimeShare = 0.75f;

// Nothing a player presses can repeat faster than the global cooldown, and the
// floor sits near 750ms even at extreme haste. Anything landing well under
// that was not pressed at all - logs report passive procs, aura ticks and
// pet contributions as casts, and one of those firing every fifth of a second
// will outnumber the real rotation several times over. The threshold is set
// below the true floor so a genuinely fast off-global ability is never caught
// by it; the intent is to exclude what is impossible, not what is merely fast.
constexpr int32_t kImpossibleCastGapMs = 500;

int32_t medianGap(std::vector<int32_t>& gaps) {
    if (gaps.empty()) return 0;
    std::sort(gaps.begin(), gaps.end());
    return gaps[gaps.size() / 2];
}

// How evenly spaced the casts were, 0..1. A spell pressed on cooldown gives
// near-identical gaps; one pressed when a mechanic demanded it does not.
double gapRegularity(const std::vector<int32_t>& gaps, int32_t median) {
    if (gaps.size() < 2 || median <= 0) return 0.0;

    double totalDeviation = 0.0;
    for (int32_t gap : gaps) {
        totalDeviation += std::abs(static_cast<double>(gap - median));
    }
    const double meanDeviation = totalDeviation / gaps.size();
    const double relative = meanDeviation / median;

    // Deviation at or beyond the tolerance reads as no rhythm at all.
    if (relative >= kRegularGapTolerance) return 0.0;
    return 1.0 - (relative / kRegularGapTolerance);
}

struct CastStats {
    int32_t count = 0;
    int32_t firstMs = 0;
    int32_t lastMs = 0;
    int32_t medianGapMs = 0;
    double regularity = 0.0;
    float castsPerMinute = 0.0f;
};

// Decide a role from cadence, and say how much to trust it.
//
// The confidence is not a probability - it orders the review queue, so what
// matters is that a shaky call sorts above a solid one. Anything the
// heuristics cannot read lands near zero and reaches the user first.
struct RoleGuess {
    CastRole role = CastRole::Unclassified;
    SuggestionBasis basis = SuggestionBasis::None;
    float confidence = 0.0f;
};

RoleGuess guessRole(const CastStats& stats, int32_t knownCooldownMs,
                    uint32_t spellId) {
    RoleGuess guess;

    // Checked before anything else. These are not weak readings of a real
    // ability - they are events that were never a global in the first place,
    // and letting them reach the cadence bands would have them counted as the
    // most-used spells in the pull.
    if (spellId == kMeleeSpellId) {
        guess.role = CastRole::Melee;
        guess.basis = SuggestionBasis::AutoAttack;
        guess.confidence = 0.95f;
        return guess;
    }
    if (stats.count >= kMinCastsForCadence &&
        stats.medianGapMs > 0 && stats.medianGapMs < kImpossibleCastGapMs) {
        guess.role = CastRole::Ignored;
        guess.basis = SuggestionBasis::FasterThanGcd;
        guess.confidence = 0.9f;
        return guess;
    }

    // A known long cooldown settles it regardless of rhythm. This is the one
    // piece of real spell data available, so it outranks inference from
    // timings - a player who only had time for two uses still used a major
    // cooldown twice.
    if (knownCooldownMs >= kMajorCooldownMs) {
        guess.role = CastRole::MajorCooldown;
        guess.basis = SuggestionBasis::CadenceMajor;
        guess.confidence = 0.8f;
        return guess;
    }

    if (stats.count == 1) {
        // One cast is a fact, not a pattern. Something used once is usually
        // situational, but it could equally be a cooldown the pull was too
        // short to repeat, so this goes to the user with low confidence.
        guess.role = CastRole::Utility;
        guess.basis = SuggestionBasis::SingleUse;
        guess.confidence = 0.15f;
        return guess;
    }

    if (stats.count < kMinCastsForCadence) {
        guess.role = knownCooldownMs > 0 ? CastRole::MinorCooldown : CastRole::Utility;
        guess.basis = SuggestionBasis::SingleUse;
        guess.confidence = 0.2f;
        return guess;
    }

    const float cpm = stats.castsPerMinute;
    const double regularity = stats.regularity;

    // Irregular usage at any rate is situational. Checked before the rate
    // bands so a spell pressed twice early and twice late does not get read
    // as a steady two-per-minute cooldown.
    if (regularity < 0.25) {
        guess.role = CastRole::Utility;
        guess.basis = SuggestionBasis::Irregular;
        guess.confidence = 0.2f;
        return guess;
    }

    if (cpm <= kMajorCooldownMaxCpm) {
        guess.role = CastRole::MajorCooldown;
        guess.basis = SuggestionBasis::CadenceMajor;
        guess.confidence = static_cast<float>(0.45 + 0.35 * regularity);
        return guess;
    }

    if (cpm <= kMinorCooldownMaxCpm) {
        guess.role = CastRole::MinorCooldown;
        guess.basis = SuggestionBasis::CadenceMinor;
        guess.confidence = static_cast<float>(0.40 + 0.35 * regularity);
        return guess;
    }

    if (cpm >= kFillerMinCpm) {
        guess.role = CastRole::Filler;
        guess.basis = SuggestionBasis::CadenceFiller;
        guess.confidence = static_cast<float>(0.50 + 0.30 * regularity);
        return guess;
    }

    // Between the minor-cooldown and filler bands. Common for resource
    // spenders, which is the suggestion offered - but the gap is exactly
    // where cadence stops being informative, so confidence stays low and the
    // user sees it early.
    guess.role = CastRole::ResourceSpender;
    guess.basis = SuggestionBasis::Irregular;
    guess.confidence = 0.25f;
    return guess;
}

} // namespace

SpecClassification suggestClassifications(const SuggesterInput& input) {
    SpecClassification spec;
    spec.specId = input.specId;
    spec.specName = input.specName;
    spec.gameVersion = input.gameVersion;

    if (input.pullDurationMs <= 0) {
        return spec;
    }
    const double pullMinutes = input.pullDurationMs / 60000.0;

    // Gather per-spell timings. Casts arrive in whatever order the database
    // held them, so each spell's list is sorted before gaps are measured.
    std::unordered_map<uint32_t, std::vector<int32_t>> castTimes;
    for (const auto& cast : input.casts) {
        if (cast.spellId == 0) continue;
        // Pre-pull casts carry negative timestamps. They are real casts, but
        // they distort cadence for a pull that had not started, so the
        // opening cast is clamped to the pull start.
        castTimes[cast.spellId].push_back(std::max(cast.timestampMs, 0));
    }

    for (auto& [spellId, times] : castTimes) {
        if (times.empty()) continue;
        std::sort(times.begin(), times.end());

        CastStats stats;
        stats.count = static_cast<int32_t>(times.size());
        stats.firstMs = times.front();
        stats.lastMs = times.back();
        stats.castsPerMinute = pullMinutes > 0.0
                                   ? static_cast<float>(stats.count / pullMinutes)
                                   : 0.0f;

        std::vector<int32_t> gaps;
        gaps.reserve(times.size() > 0 ? times.size() - 1 : 0);
        for (size_t i = 1; i < times.size(); ++i) {
            gaps.push_back(times[i] - times[i - 1]);
        }
        stats.medianGapMs = medianGap(gaps);
        stats.regularity = gapRegularity(gaps, stats.medianGapMs);

        int32_t knownCd = 0;
        if (auto it = input.cooldownMs.find(spellId); it != input.cooldownMs.end()) {
            knownCd = it->second;
        }

        const RoleGuess guess = guessRole(stats, knownCd, spellId);

        SpellClass spell;
        spell.spellId = spellId;
        if (auto it = input.spellNames.find(spellId); it != input.spellNames.end()) {
            spell.name = it->second;
        }
        spell.role = guess.role;
        spell.confirmed = false;   // Always. The user decides.
        spell.basis = guess.basis;
        spell.suggestionConfidence = guess.confidence;
        spell.castsPerMinute = stats.castsPerMinute;
        spell.observedCasts = stats.count;
        spell.medianGapMs = stats.medianGapMs;
        spell.assumedWindowMs = knownCd;

        spec.spells[spellId] = std::move(spell);
    }

    // Auras the player put on themselves. Anything applied by someone else is
    // outside the player's control and would only add noise to a review list
    // that is already long.
    struct AuraStats {
        int32_t applications = 0;
        int32_t totalUptimeMs = 0;
        int32_t longestMs = 0;
    };
    std::unordered_map<uint32_t, AuraStats> auraStats;

    for (const auto& aura : input.auras) {
        if (aura.spellId == 0 || !aura.selfApplied) continue;

        const int32_t start = std::max(aura.appliedMs, 0);
        const int32_t end = std::min(aura.removedMs, input.pullDurationMs);
        const int32_t duration = end > start ? end - start : 0;

        auto& stats = auraStats[aura.spellId];
        stats.applications += 1;
        stats.totalUptimeMs += duration;
        stats.longestMs = std::max(stats.longestMs, duration);
    }

    for (const auto& [auraId, stats] : auraStats) {
        AuraClass aura;
        aura.spellId = auraId;
        if (auto it = input.spellNames.find(auraId); it != input.spellNames.end()) {
            aura.name = it->second;
        }
        aura.applications = stats.applications;
        aura.totalUptimeMs = stats.totalUptimeMs;
        aura.uptimeShare = static_cast<float>(
            static_cast<double>(stats.totalUptimeMs) / input.pullDurationMs);
        aura.basis = SuggestionBasis::AuraSelfBuff;
        aura.confirmed = false;

        // An aura that is up nearly always and was refreshed repeatedly is
        // something the player maintains. One that comes and goes in short
        // bursts is a proc. A single long window usually belongs to a
        // cooldown. All three are guesses about intent, so none is confident.
        if (aura.uptimeShare >= 0.80f && stats.applications >= 2) {
            aura.role = AuraRole::Maintenance;
            aura.suggestionConfidence = 0.5f;
        } else if (stats.applications >= 4 && stats.longestMs < kMaintenanceMinDurationMs) {
            aura.role = AuraRole::Proc;
            aura.suggestionConfidence = 0.4f;
        } else if (stats.applications <= 3 && stats.longestMs >= kMaintenanceMinDurationMs &&
                   aura.uptimeShare < kMaxCooldownUptimeShare) {
            aura.role = AuraRole::CooldownBuff;
            aura.suggestionConfidence = 0.35f;
        } else {
            aura.role = AuraRole::Unimportant;
            aura.suggestionConfidence = 0.15f;
        }

        spec.auras[auraId] = std::move(aura);
    }

    // Link each major cooldown to the buff that marks its window, so the
    // burst/filler split can follow real uptime instead of an assumed
    // duration. Matching by shared id is narrow but never wrong; anything
    // subtler is a guess, and the user can set it in review.
    for (auto& [spellId, spell] : spec.spells) {
        if (spell.role != CastRole::MajorCooldown) continue;
        if (spec.auras.count(spellId)) {
            spell.windowAuraSpellId = spellId;
        }
    }

    return spec;
}

} // namespace benchmark
