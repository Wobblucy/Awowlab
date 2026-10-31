#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/PhaseSettings.h"

namespace benchmark {

// A compact summary of one player's performance in one pull, cut into the
// user's own phase windows. The mentor log is reduced to this at import time
// and its raw events are dropped, so the app never has to hold two encounters
// at once.
//
// A progression pull usually ends at the user's death or a wipe, while the
// mentor's pull ran to a kill. Comparing the two in full would mostly measure
// how long each player lived. The comparison therefore truncates the mentor at
// the user's death, and the profile keeps a coarse timeline (see
// ProfileTimelineBucket) next to the per-phase aggregates so it can be re-cut
// at any time without the raw events.
//
// Times are stored relative to the start of the phase they fall in. Phases
// start at different times and run different lengths from pull to pull, so
// absolute timestamps would not line up.
//
// The profile also records which phase rules fired when it was built. A rule
// that never fired leaves a window with nothing to compare against, and the
// comparison reports that instead of diffing mismatched segments.

// Per-spell cast activity within a phase. Casts per minute is the comparable
// figure, since the same phase can run 40s in one pull and 70s in another; the
// raw count is for display.
struct ProfileSpellUsage {
    uint32_t spellId = 0;
    int32_t casts = 0;
    float castsPerMinute = 0.0f;
    // First use within the phase. Mostly of interest for cooldowns, where
    // holding one 20s into a burn window plays differently from opening with it.
    int32_t firstCastOffsetMs = -1;
    bool isCooldown = false;  // Has a real cooldown (from SpellGCDDatabase)

    // Damage this spell did and the cast time it took. Damage per second of
    // casting ranks a slow nuke against a cheap instant fairly, where total
    // damage alone would not.
    int64_t damage = 0;
    int32_t castTimeMs = 0;   // Summed cast time; 0 for purely instant spells

    double damagePerCastSecond() const {
        return castTimeMs > 0 ? (double)damage * 1000.0 / castTimeMs : 0.0;
    }
    int64_t damagePerCast() const {
        return casts > 0 ? damage / casts : 0;
    }
};

// Damage the player dealt, grouped by the spell id that dealt it. Kept apart
// from ProfileSpellUsage, which is keyed by the spell cast: many abilities deal
// their damage under a different id (a projectile, a proc, a dot tick), and
// folding damage into the cast map dropped everything that did not share an id.
struct ProfileDamageBySpell {
    uint32_t spellId = 0;
    int64_t total = 0;
    int32_t hits = 0;
};

// One defensive use and the damage taken while it was up. Recorded per use
// rather than as a total, so holding all defensives to the end and using them
// all at the start show up as different things.
struct ProfileDefensiveUse {
    uint32_t spellId = 0;
    int32_t pullOffsetMs = 0;
    int32_t windowMs = 0;
    int64_t damageTakenInWindow = 0;
    int64_t absorbedInWindow = 0;
};

// A stretch where the player was not casting, with what was going on at the
// time. A cause is recorded only where the log shows one; otherwise the fields
// stay empty rather than guessing.
struct ProfileDowntimeGap {
    int32_t startMs = 0;
    int32_t durationMs = 0;

    // Set when the gap begins with a cast that was started and abandoned.
    bool cancelledCast = false;
    uint32_t cancelledSpellId = 0;

    // Debuffs on the player for the whole gap, e.g. a root or silence that
    // explains it.
    std::vector<uint32_t> debuffSpellIds;

    // Whether a burst window was open during the gap.
    bool inCooldown = false;

    // The casts on either side of the gap, and how long the global before it
    // should have lasted.
    uint32_t beforeSpellId = 0;
    uint32_t afterSpellId = 0;
    int32_t expectedGcdMs = 0;

    // The casts leading into the gap (about ten seconds), oldest first, with
    // how long each one occupied.
    struct PriorCast {
        uint32_t spellId = 0;
        int32_t timeMs = 0;
        int32_t occupancyMs = 0;
    };
    std::vector<PriorCast> leadIn;
};

// The player's position, sampled through the pull rather than kept per event.
// Boss coordinates are stored alongside so the path can be drawn relative to
// the boss, which is what lines up between two pulls.
struct ProfilePosition {
    int32_t timeMs = 0;
    float x = 0.0f;
    float y = 0.0f;
    float bossX = 0.0f;
    float bossY = 0.0f;
    bool hasBoss = false;
};

// Damage taken from one ability across a phase. Aggregated because individual
// hits vary too much between pulls to compare one by one.
struct ProfileDamageTaken {
    uint32_t spellId = 0;
    int32_t hits = 0;
    int64_t total = 0;
};

// Damage the player dealt, grouped by target npc id. Target choice (cleaving
// adds or staying on the boss) often explains the difference between two pulls,
// and npc ids do not depend on the client language.
struct ProfileDamageDone {
    uint32_t npcId = 0;
    int64_t total = 0;
    int32_t casts = 0;
};

// Coarse pull-relative timeline, used to sum the mentor up to the user's death
// and to draw a rough shape on the replay timeline. It is not used for judging
// timing: comparing bucket to bucket would penalise a player for doing the
// right thing a few seconds off the mentor's clock. Timing comparisons go
// through ProfileCooldownUse.
struct ProfileTimelineBucket {
    int32_t startMs = 0;         // Pull-relative
    int64_t damageDone = 0;
    int64_t damageTaken = 0;
    int32_t casts = 0;
};

// One use of a cooldown and what was done while it was up.
//
// The window is anchored to the cooldown rather than to fixed time buckets.
// Pressing a 20s cooldown three seconds later than the mentor would otherwise
// make one bucket look bad and the next look good, when the output was the
// same and only shifted. This keeps "when was it pressed" and "how much did it
// produce" as separate questions.
//
// Uses are numbered per spell so the nth use is compared with the mentor's nth
// use rather than matched by clock time.
struct ProfileCooldownUse {
    uint32_t spellId = 0;
    int32_t useIndex = 0;        // 1st, 2nd, 3rd use of this spell in the pull
    int32_t pullOffsetMs = 0;    // When it went out
    int32_t windowMs = 0;        // How long the effect was up

    int64_t damageInWindow = 0;
    int32_t castsInWindow = 0;

    // The casts in the order they went out. Two windows with the same damage
    // and cast count can still have been played differently.
    std::vector<uint32_t> castSequence;

    // Every cooldown buff that was up during this window, in the order they
    // came up (a cooldown, a trinket and a potion can share one window).
    std::vector<uint32_t> stackedSpellIds;

    // Damage dealt in the window, sampled once a second from when it opened.
    // Anchored to the window so two players' bursts line up even when they
    // pressed at different times. A longer window has more samples.
    std::vector<int64_t> damageCurve;

    // Where the window's damage went, biggest target first.
    std::vector<ProfileDamageDone> damageByTarget;

    // Buffs that overlapped this window, longest first. An external buff in
    // one player's window and not the other's is a common reason two uses of
    // the same cooldown differ, so it is recorded directly.
    struct WindowBuff {
        uint32_t spellId = 0;
        int32_t overlapMs = 0;   // Time this buff was up inside the window
        // Start of the buff relative to the window's start, i.e. whether it
        // covered the opening of the burst or landed late.
        int32_t offsetMs = 0;
    };
    std::vector<WindowBuff> buffs;
};

// Throughput split by whether a major cooldown was up.
//
// "Major cooldown" means whatever the user confirmed as one for this spec (see
// SpecClassification), not a guess from cooldown length. Window boundaries come
// from the cooldown's own buff where one exists.
//
// Most of a pull is spent outside burst windows, so judging only the windows
// would miss most of the damage. The two regions are kept apart because a
// dropped global costs more inside a burst window, and the fixes differ.
struct ProfileThroughputSplit {
    int32_t durationMs = 0;
    int64_t damage = 0;
    int32_t casts = 0;
    // Time with no cast going out and no GCD rolling. Compared as a share of
    // the region so it still works after truncation.
    int32_t idleMs = 0;

    double dps() const {
        return durationMs > 0 ? (double)damage * 1000.0 / durationMs : 0.0;
    }
    double castsPerMinute() const {
        return durationMs > 0 ? (double)casts * 60000.0 / durationMs : 0.0;
    }
    double idleShare() const {
        return durationMs > 0 ? (double)idleMs / durationMs : 0.0;
    }
};

// Everything recorded for one phase window.
struct ProfilePhase {
    std::string label;           // Matches the phase rule's label ("P2", "Burn", ...)
    int32_t durationMs = 0;      // How long this phase ran in the mentor's pull

    std::vector<ProfileSpellUsage> spells;
    std::vector<ProfileDamageTaken> damageTaken;
    std::vector<ProfileDamageDone> damageDone;  // Split by target npcId
    std::vector<ProfileDamageBySpell> damageBySpell;  // Split by source spell

    // Damage that never landed because a shield absorbed it or the attack
    // missed.
    int64_t totalAbsorbed = 0;
    int64_t totalAvoided = 0;

    // Same phase, split by whether a major cooldown was active.
    ProfileThroughputSplit inCooldowns;
    ProfileThroughputSplit outsideCooldowns;

    int64_t totalDamageDone = 0;
    int64_t totalDamageTaken = 0;
    int32_t deaths = 0;
};

// The mentor performance being compared against.
struct MentorProfile {
    std::string sourceLabel;      // Report code / file the profile came from

    // Enough to find the same pull again. A profile has to be rebuilt from the
    // original events whenever a spell ruling changes, and without this the
    // user would have to pick the log, fight and player again by hand.
    struct Origin {
        // Cached WCL report code, or empty when the source was a local file.
        std::string reportCode;
        // Local log path, or empty when the source was a report.
        std::string logPath;
        int32_t fightId = 0;
        std::string actorGuid;

        bool isValid() const {
            return (!reportCode.empty() || !logPath.empty()) && !actorGuid.empty();
        }
    };
    Origin origin;
    std::string playerName;       // Display only; matching never uses names

    // The actor this profile describes. Separate from Origin, which says where
    // to find the log; this is known even when no source was recorded.
    std::string actorGuid;
    uint32_t encounterId = 0;
    uint16_t specId = 0;          // Profiles are only compared within a spec
    uint32_t difficulty = 0;
    int32_t pullDurationMs = 0;
    bool kill = false;

    // The phase rules this profile was cut with. The comparison checks they
    // still match the user's current rules.
    std::vector<PhaseSettings::PhaseRule> phaseRules;

    // The rulings in force when this profile was cut. Each one changes what
    // the stored numbers mean, so if the user's current rulings differ the
    // profile needs a rebuild rather than a comparison.
    std::vector<uint32_t> majorCooldownSpellIds;  // Defines burst vs filler
    std::vector<uint32_t> ignoredSpellIds;        // Spells ruled not worth measuring
    std::vector<uint32_t> offGlobalSpellIds;      // Affects the globals count
    std::vector<uint32_t> ignoredAuraIds;         // Affects buffs shown per window

    // The mob weights in force when this profile was cut. Every damage figure
    // here has them applied, so a change means a rebuild.
    uint64_t mobWeightFingerprint = 0;

    // Everything that opened a burst window when this profile was cut: major
    // cooldowns, trinkets, and buffs ruled a cooldown in their own right. Kept
    // apart from majorCooldownSpellIds because these decide where the windows
    // are, and a change to them has to be detected.
    std::vector<uint32_t> burstWindowSpellIds;
    std::vector<uint32_t> buffOnlyCooldownIds;

    // Rules that did not fire in the mentor's pull. Their windows have no
    // mentor counterpart and are reported as not comparable.
    std::vector<std::string> unresolvedPhaseLabels;

    std::vector<ProfilePhase> phases;

    // Pull-relative timeline, ordered by startMs.
    int32_t bucketMs = 5000;
    std::vector<ProfileTimelineBucket> timeline;

    // Every cooldown use in the pull, pull-relative and ordered.
    std::vector<ProfileCooldownUse> cooldownUses;

    // Every defensive the player pressed, in order.
    std::vector<ProfileDefensiveUse> defensiveUses;

    // The player's path, one sample per second.
    std::vector<ProfilePosition> positions;

    // Every stretch without a cast, longest first.
    std::vector<ProfileDowntimeGap> downtimeGaps;

    // Damage after per-npc weighting, and the raw figure. The weighted number
    // is what gets compared; the raw one matches what other tools show.
    int64_t weightedDamageDone = 0;
    int64_t rawDamageDone = 0;

    bool isValid() const { return encounterId != 0 && !timeline.empty(); }

    // Mentor's totals over [0, cutoffMs), i.e. over the same time the user
    // survived.
    struct Window {
        int32_t durationMs = 0;
        int64_t damageDone = 0;
        int64_t damageTaken = 0;
        int32_t casts = 0;

        // The same window split by cooldown state.
        ProfileThroughputSplit inCooldowns;
        ProfileThroughputSplit outsideCooldowns;

        double dps() const {
            return durationMs > 0 ? (double)damageDone * 1000.0 / durationMs : 0.0;
        }
        double castsPerMinute() const {
            return durationMs > 0 ? (double)casts * 60000.0 / durationMs : 0.0;
        }
    };
    Window windowUpTo(int32_t cutoffMs) const;

    // Cooldowns the mentor used before the cutoff.
    std::vector<ProfileCooldownUse> cooldownsUpTo(int32_t cutoffMs) const;

    std::string toJson() const;
    static std::optional<MentorProfile> fromJson(const std::string& json);
};

} // namespace benchmark
