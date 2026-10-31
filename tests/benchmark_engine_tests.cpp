#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Core/Benchmark/BenchmarkEngine.h"

using namespace benchmark;

namespace {

// Two profiles of the same boss and spec, cut with the same rules, so the
// comparison has nothing to object to before it starts.
struct ComparablePair {
    MentorProfile user;
    MentorProfile mentor;
    SpecClassification classification;
};

// Damage figures are scaled to realistic per-5s-bucket numbers. The engine
// holds findings to an absolute floor as well as a ratio, so toy values would
// exercise the ratio and silently skip everything behind that floor.
constexpr int64_t kDamageScale = 1000;

MentorProfile makeSideProfile(int64_t damagePerBucket, int32_t durationMs) {
    damagePerBucket *= kDamageScale;

    MentorProfile profile;
    profile.encounterId = 3009;
    profile.specId = 250;
    profile.pullDurationMs = durationMs;
    profile.bucketMs = 5000;

    const int32_t buckets = durationMs / 5000;
    for (int32_t i = 0; i < buckets; ++i) {
        ProfileTimelineBucket bucket;
        bucket.startMs = i * 5000;
        bucket.damageDone = damagePerBucket;
        bucket.damageTaken = 100;
        bucket.casts = 10;
        profile.timeline.push_back(bucket);
    }

    ProfilePhase phase;
    phase.label = "P1";
    phase.durationMs = durationMs;
    phase.totalDamageDone = damagePerBucket * buckets;
    phase.outsideCooldowns.durationMs = durationMs;
    phase.outsideCooldowns.damage = phase.totalDamageDone;
    phase.outsideCooldowns.casts = 10 * buckets;
    profile.phases.push_back(phase);

    return profile;
}

ComparablePair makePair(int64_t userDamage = 1000, int64_t mentorDamage = 1000,
                        int32_t durationMs = 60000) {
    ComparablePair pair;
    pair.user = makeSideProfile(userDamage, durationMs);
    pair.mentor = makeSideProfile(mentorDamage, durationMs);
    pair.classification.specId = 250;
    return pair;
}

ComparisonRequest makeRequest(ComparablePair& pair, int32_t deathMs = -1) {
    ComparisonRequest request;
    request.user = &pair.user;
    request.mentor = &pair.mentor;
    request.classification = &pair.classification;
    request.userDeathMs = deathMs;
    return request;
}

void addConfirmedCooldown(ComparablePair& pair, uint32_t spellId) {
    SpellClass cd;
    cd.spellId = spellId;
    cd.role = CastRole::MajorCooldown;
    cd.confirmed = true;
    pair.classification.spells[spellId] = cd;
}

bool hasFinding(const BenchmarkResult& result, FindingKind kind) {
    for (const auto& f : result.overall) {
        if (f.kind == kind) return true;
    }
    return false;
}

const Finding* findFinding(const BenchmarkResult& result, FindingKind kind) {
    for (const auto& f : result.overall) {
        if (f.kind == kind) return &f;
    }
    return nullptr;
}

} // namespace

// Blocking - the comparison refuses rather than degrades

TEST_CASE("Comparison refuses to run on unconfirmed classifications",
          "[benchmark][engine][blocking]") {
    ComparablePair pair = makePair();
    SpellClass pending;
    pending.spellId = 100;
    pending.confirmed = false;
    pair.classification.spells[100] = pending;
    pair.classification.pendingSpellIds = {100};

    const auto result = compare(makeRequest(pair));

    // Guessing here would misreport every window built on that spell, and
    // nothing in the output would reveal it.
    CHECK_FALSE(result.valid);
    CHECK(result.blockingReason == "unconfirmed_spells");
    REQUIRE(result.unclassifiedSpellIds.size() == 1);
    CHECK(result.unclassifiedSpellIds[0] == 100);
}

TEST_CASE("Comparison blocks across specs", "[benchmark][engine][blocking]") {
    ComparablePair pair = makePair();
    pair.mentor.specId = 251;

    const auto result = compare(makeRequest(pair));

    CHECK_FALSE(result.valid);
    CHECK(result.blockingReason == "spec_mismatch");
}

TEST_CASE("Comparison blocks across encounters", "[benchmark][engine][blocking]") {
    ComparablePair pair = makePair();
    pair.mentor.encounterId = 9999;

    const auto result = compare(makeRequest(pair));

    CHECK_FALSE(result.valid);
    CHECK(result.blockingReason == "encounter_mismatch");
}

TEST_CASE("Comparison blocks when the phase rules have since changed",
          "[benchmark][engine][blocking]") {
    ComparablePair pair = makePair();
    PhaseSettings::PhaseRule rule;
    rule.kind = PhaseSettings::RuleKind::SpellCast;
    rule.spellId = 4242;
    pair.user.phaseRules.push_back(rule);
    // The reference was cut without that rule, so the windows no longer
    // describe the same stretches of fight.

    const auto result = compare(makeRequest(pair));

    CHECK_FALSE(result.valid);
    CHECK(result.blockingReason == "phase_rules_changed");
}

TEST_CASE("A missing profile blocks rather than crashing",
          "[benchmark][engine][blocking]") {
    ComparablePair pair = makePair();
    ComparisonRequest request = makeRequest(pair);
    request.mentor = nullptr;

    const auto result = compare(request);

    CHECK_FALSE(result.valid);
    CHECK(result.blockingReason == "missing_profile");
}

// Truncation - the fairness guarantee, end to end

TEST_CASE("The reference is measured over the window the user survived",
          "[benchmark][engine][truncation]") {
    // The reference lived twice as long at exactly the same rate.
    ComparablePair pair;
    pair.user = makeSideProfile(1000, 30000);
    pair.mentor = makeSideProfile(1000, 60000);
    pair.classification.specId = 250;

    const auto result = compare(makeRequest(pair, 30000));

    REQUIRE(result.valid);
    // Same rate over the same window means no gap, even though the reference's
    // pull total is double. Without truncation this would report a 50%
    // shortfall that says nothing about how either pull was played.
    CHECK_THAT(result.summary.dpsDelta, Catch::Matchers::WithinAbs(0.0, 1.0));
    CHECK(result.summary.userDied);
    CHECK(result.summary.userDeathMs == 30000);
    CHECK(result.summary.comparedDurationMs == 30000);
}

TEST_CASE("Surviving the pull compares the whole of it",
          "[benchmark][engine][truncation]") {
    ComparablePair pair = makePair(1000, 1000, 60000);

    const auto result = compare(makeRequest(pair, -1));

    REQUIRE(result.valid);
    CHECK_FALSE(result.summary.userDied);
    CHECK(result.summary.comparedDurationMs == 60000);
}

// Cooldowns - timing and output are separate lessons

TEST_CASE("A cooldown shifted but equally productive is a timing finding only",
          "[benchmark][engine][cooldowns]") {
    ComparablePair pair = makePair();
    addConfirmedCooldown(pair, 100);

    // Identical output, pressed 8 seconds later.
    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 1, 18000, 20000, 1000000, 12});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(hasFinding(result, FindingKind::CooldownDelayed));
    // The damage was the same. Reporting a loss here would be wrong, and would
    // teach the user to distrust everything else in the report.
    CHECK_FALSE(hasFinding(result, FindingKind::CooldownWindowWeaker));
}

TEST_CASE("A cooldown pressed on time for less output is a quality finding",
          "[benchmark][engine][cooldowns]") {
    ComparablePair pair = makePair();
    addConfirmedCooldown(pair, 100);

    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 1, 10200, 20000, 400000, 7});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(hasFinding(result, FindingKind::CooldownWindowWeaker));
    CHECK_FALSE(hasFinding(result, FindingKind::CooldownDelayed));
}

TEST_CASE("A cooldown the reference used and the user never did is reported",
          "[benchmark][engine][cooldowns]") {
    ComparablePair pair = makePair();
    addConfirmedCooldown(pair, 100);
    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 1000000, 12});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    const auto* finding = findFinding(result, FindingKind::CooldownUnused);
    REQUIRE(finding != nullptr);
    CHECK(finding->weight == FindingWeight::Major);
    CHECK(finding->spellId == 100);
}

TEST_CASE("Fewer uses of a cooldown than the reference is reported",
          "[benchmark][engine][cooldowns]") {
    ComparablePair pair = makePair(1000, 1000, 180000);
    addConfirmedCooldown(pair, 100);

    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 1000000, 12});
    pair.mentor.cooldownUses.push_back({100, 2, 70000, 20000, 1000000, 12});
    pair.mentor.cooldownUses.push_back({100, 3, 130000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 1, 10000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 2, 70000, 20000, 1000000, 12});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    const auto* finding = findFinding(result, FindingKind::CooldownFewerUses);
    REQUIRE(finding != nullptr);
    CHECK(finding->userValue == 2.0);
    CHECK(finding->mentorValue == 3.0);
}

TEST_CASE("Cooldowns are matched nth use against nth use",
          "[benchmark][engine][cooldowns]") {
    ComparablePair pair = makePair(1000, 1000, 120000);
    addConfirmedCooldown(pair, 100);

    // The user opened late but caught up; the second uses nearly align.
    pair.mentor.cooldownUses.push_back({100, 1, 5000, 20000, 1000000, 12});
    pair.mentor.cooldownUses.push_back({100, 2, 65000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 1, 30000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 2, 66000, 20000, 1000000, 12});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // Exactly one delay finding: the first use. Lining these up by clock
    // rather than by use index would flag the second as well, which went out
    // on time.
    int delayed = 0;
    for (const auto& f : result.overall) {
        if (f.kind == FindingKind::CooldownDelayed) ++delayed;
    }
    CHECK(delayed == 1);
}

TEST_CASE("A cooldown pressed slightly early is not reported as a difference",
          "[benchmark][engine][cooldowns]") {
    ComparablePair pair = makePair();
    addConfirmedCooldown(pair, 100);

    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 1000000, 12});
    pair.user.cooldownUses.push_back({100, 1, 9000, 20000, 1000000, 12});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // A second apart is the same moment in a fight. Reporting it would bury
    // the findings that matter.
    CHECK_FALSE(hasFinding(result, FindingKind::CooldownDelayed));
}

// Throughput and the rest

TEST_CASE("Identical pulls produce no findings at all", "[benchmark][engine]") {
    ComparablePair pair = makePair(1000, 1000);

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // Two identical performances have nothing to say to each other. A report
    // that manufactures advice here is noise, and noise gets ignored.
    CHECK(result.overall.empty());
    CHECK_THAT(result.summary.dpsDelta, Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("Filler throughput below the reference is reported",
          "[benchmark][engine][filler]") {
    // 20% behind across the pull, all of it outside cooldowns.
    ComparablePair pair = makePair(800, 1000);

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(hasFinding(result, FindingKind::FillerDpsLower));
    CHECK(result.summary.fillerDamageDelta < 0);
}

TEST_CASE("Filler within the tolerance is left alone",
          "[benchmark][engine][filler]") {
    // 3% behind - two pulls are never identical, and a report that flags this
    // drowns the ones that matter.
    ComparablePair pair = makePair(970, 1000);

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK_FALSE(hasFinding(result, FindingKind::FillerDpsLower));
}

TEST_CASE("The summary attributes the shortfall to burst or filler",
          "[benchmark][engine]") {
    ComparablePair pair = makePair(1000, 1000);

    // Equal totals, but the reference got more of it inside cooldowns.
    pair.user.phases[0].inCooldowns = {20000, 2000 * kDamageScale, 20, 0};
    pair.user.phases[0].outsideCooldowns = {40000, 10000 * kDamageScale, 80, 4000};
    pair.mentor.phases[0].inCooldowns = {20000, 6000 * kDamageScale, 20, 0};
    pair.mentor.phases[0].outsideCooldowns = {40000, 6000 * kDamageScale, 80, 1000};

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // Behind inside cooldowns, ahead outside them. One averaged dps number
    // would hide which region actually needs the work.
    CHECK(result.summary.burstDamageDelta < 0);
    CHECK(result.summary.fillerDamageDelta > 0);
}

TEST_CASE("More idle time outside cooldowns is reported",
          "[benchmark][engine][filler]") {
    ComparablePair pair = makePair(1000, 1000);
    pair.user.phases[0].outsideCooldowns = {60000, 12000 * kDamageScale, 100, 12000};
    pair.mentor.phases[0].outsideCooldowns = {60000, 12000 * kDamageScale, 100, 1200};

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(hasFinding(result, FindingKind::FillerIdleHigher));
}

TEST_CASE("Damage taken above the reference is reported as a difference",
          "[benchmark][engine][damagetaken]") {
    ComparablePair pair = makePair();
    pair.user.phases[0].damageTaken.push_back({7777, 4, 4000000});
    pair.mentor.phases[0].damageTaken.push_back({7777, 1, 500000});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    const auto* finding = findFinding(result, FindingKind::DamageTakenHigher);
    REQUIRE(finding != nullptr);
    CHECK(finding->spellId == 7777);
    CHECK(finding->userValue > finding->mentorValue);
}

TEST_CASE("Comparable damage taken is not reported",
          "[benchmark][engine][damagetaken]") {
    ComparablePair pair = makePair();
    pair.user.phases[0].damageTaken.push_back({7777, 3, 1100000});
    pair.mentor.phases[0].damageTaken.push_back({7777, 3, 1000000});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // Both ate the mechanic about the same number of times. There is no
    // lesson in a 10% difference on damage taken.
    CHECK_FALSE(hasFinding(result, FindingKind::DamageTakenHigher));
}

TEST_CASE("Cooldown spells do not also appear as cast-rate findings",
          "[benchmark][engine]") {
    ComparablePair pair = makePair();
    addConfirmedCooldown(pair, 100);

    pair.mentor.phases[0].spells.push_back({100, 12, 12.0f, 1000, true});
    pair.user.phases[0].spells.push_back({100, 2, 2.0f, 1000, true});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // Cooldowns get their own findings, with the timing and output detail that
    // makes them actionable. A bare rate would restate that less usefully.
    CHECK_FALSE(hasFinding(result, FindingKind::CastRateLower));
}

TEST_CASE("A filler cast well below the reference rate is reported",
          "[benchmark][engine]") {
    ComparablePair pair = makePair();

    SpellClass filler;
    filler.spellId = 200;
    filler.role = CastRole::Filler;
    filler.confirmed = true;
    pair.classification.spells[200] = filler;

    pair.mentor.phases[0].spells.push_back({200, 30, 30.0f, 1000, false});
    pair.user.phases[0].spells.push_back({200, 10, 10.0f, 1000, false});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(hasFinding(result, FindingKind::CastRateLower));
}

TEST_CASE("Utility spells are left out of cast-rate findings",
          "[benchmark][engine]") {
    ComparablePair pair = makePair();

    SpellClass utility;
    utility.spellId = 300;
    utility.role = CastRole::Utility;
    utility.confirmed = true;
    pair.classification.spells[300] = utility;

    pair.mentor.phases[0].spells.push_back({300, 20, 20.0f, 1000, false});
    pair.user.phases[0].spells.push_back({300, 2, 2.0f, 1000, false});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    // Utility use follows what the fight demanded of each player, which the
    // log cannot see. Calling it a shortfall would be guessing.
    CHECK_FALSE(hasFinding(result, FindingKind::CastRateLower));
}

TEST_CASE("A different damage split across targets is reported",
          "[benchmark][engine][targets]") {
    ComparablePair pair = makePair();

    // The reference cleaved; the user tunnelled. Shares are taken against the
    // phase total, so these have to add up to it.
    const int64_t total = pair.user.phases[0].totalDamageDone;
    pair.user.phases[0].damageDone.push_back({1001, total * 9 / 10, 100});
    pair.user.phases[0].damageDone.push_back({1002, total / 10, 10});
    pair.mentor.phases[0].damageDone.push_back({1001, total / 2, 60});
    pair.mentor.phases[0].damageDone.push_back({1002, total / 2, 60});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(hasFinding(result, FindingKind::TargetSelection));
}

TEST_CASE("Findings are ordered with the heaviest first", "[benchmark][engine]") {
    ComparablePair pair = makePair(700, 1000);
    addConfirmedCooldown(pair, 100);

    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 5000000, 12});
    pair.user.phases[0].damageTaken.push_back({7777, 2, 200000});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    REQUIRE(result.overall.size() >= 2);
    // A report read top-down should open on whatever most likely changed the
    // outcome.
    for (size_t i = 1; i < result.overall.size(); ++i) {
        CHECK(result.overall[i - 1].weight >= result.overall[i].weight);
    }
}

TEST_CASE("A phase neither pull reached is marked uncomparable",
          "[benchmark][engine][phases]") {
    ComparablePair pair = makePair();

    ProfilePhase late;
    late.label = "P3";
    late.durationMs = 10000;
    pair.user.phases.push_back(late);
    pair.mentor.phases.push_back(late);
    pair.mentor.unresolvedPhaseLabels.push_back("P3");

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    REQUIRE(result.phases.size() == 2);
    CHECK(result.phases[0].comparable);
    // Diffing windows that do not describe the same stretch of fight would
    // look authoritative and be wrong.
    CHECK_FALSE(result.phases[1].comparable);
    CHECK(result.phases[1].uncomparableReason == "phase_not_reached");
}

TEST_CASE("countAtLeast counts only findings at or above the weight",
          "[benchmark][engine]") {
    ComparablePair pair = makePair(700, 1000);
    addConfirmedCooldown(pair, 100);
    pair.mentor.cooldownUses.push_back({100, 1, 10000, 20000, 5000000, 12});

    const auto result = compare(makeRequest(pair));

    REQUIRE(result.valid);
    CHECK(result.countAtLeast(FindingWeight::Info) >= result.countAtLeast(FindingWeight::Major));
}

TEST_CASE("Cast rates and damage taken stop at the user's death too",
          "[benchmark][engine][truncation]") {
    // The user died at 3:00 in P1. The reference cast the same 20/min there
    // and went on to a P2 the user never saw. Counting the reference's P2
    // casts against three minutes would invent a cast-rate gap, and counting
    // its P2 damage taken would hide that the user took three times as much.
    ComparablePair pair;
    pair.user = makeSideProfile(1000, 180000);
    pair.mentor = makeSideProfile(1000, 480000);
    pair.classification.specId = 250;

    auto setPhase = [](ProfilePhase& phase, int32_t casts, int64_t taken) {
        ProfileSpellUsage spell;
        spell.spellId = 100;
        spell.casts = casts;
        phase.spells = {spell};
        ProfileDamageTaken hit;
        hit.spellId = 900;
        hit.hits = 10;
        hit.total = taken;
        phase.damageTaken = {hit};
    };
    setPhase(pair.user.phases[0], 60, 3000000);

    pair.mentor.phases[0].durationMs = 180000;
    setPhase(pair.mentor.phases[0], 60, 1000000);
    ProfilePhase p2;
    p2.label = "P2";
    p2.durationMs = 300000;
    setPhase(p2, 100, 4000000);
    pair.mentor.phases.push_back(p2);

    const auto result = compare(makeRequest(pair, 180000));

    REQUIRE(result.valid);
    CHECK_FALSE(hasFinding(result, FindingKind::CastRateLower));
    const Finding* taken = findFinding(result, FindingKind::DamageTakenHigher);
    REQUIRE(taken != nullptr);
    CHECK(taken->mentorValue == 1000000.0);
}
