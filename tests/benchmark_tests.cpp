#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Core/Benchmark/BenchmarkStore.h"
#include "Core/Benchmark/ClassificationSuggester.h"
#include "Core/Benchmark/ProfileExtractor.h"
#include "Core/Benchmark/MentorProfile.h"
#include "Core/Benchmark/SpellClassification.h"
#include "Parser/SpecializationColors.h"
#include "Core/MobWeightSettings.h"
#include "Core/Benchmark/LivePullBridge.h"
#include "EventTypes.h"
#include "Core/StringInterner.h"
#include "ShippedHasteBuffs.h"

#include <filesystem>
#include <set>

using namespace benchmark;

namespace {

// The classification store saves itself on every ruling, and these tests rule
// freely - so the store is pointed at a scratch directory before anything
// runs. Without this a test run from an install directory would overwrite the
// user's real classifications.json with test data.
[[maybe_unused]] const bool kStoreRedirected = [] {
    benchmark::store::setRootDirectoryForTesting(
        (std::filesystem::temp_directory_path() / "awowlab-test-store").string());
    return true;
}();

// A profile with a flat 1000 damage per 5s bucket, which makes every
// truncation result checkable by hand.
MentorProfile makeFlatProfile(int32_t durationMs = 60000) {
    MentorProfile profile;
    profile.encounterId = 2001;
    profile.specId = 250;
    profile.pullDurationMs = durationMs;
    profile.bucketMs = 5000;

    for (int32_t t = 0; t < durationMs; t += 5000) {
        ProfileTimelineBucket bucket;
        bucket.startMs = t;
        bucket.damageDone = 1000;
        bucket.damageTaken = 200;
        bucket.casts = 10;
        profile.timeline.push_back(bucket);
    }
    return profile;
}

} // namespace

// Truncation - the fairness guarantee of the whole comparison

TEST_CASE("Mentor truncation sums only the buckets before the cutoff",
          "[benchmark][profile][truncation]") {
    const MentorProfile profile = makeFlatProfile(60000);

    // 30s into a 60s pull is exactly six 5s buckets.
    const auto window = profile.windowUpTo(30000);

    CHECK(window.durationMs == 30000);
    CHECK(window.damageDone == 6000);
    CHECK(window.damageTaken == 1200);
    CHECK(window.casts == 60);
}

TEST_CASE("A cutoff inside a bucket credits only the elapsed part",
          "[benchmark][profile][truncation]") {
    const MentorProfile profile = makeFlatProfile(60000);

    // Dying 2.5s into the third bucket should count two whole buckets plus
    // half of the third - not three whole ones, which would hand the mentor
    // damage from time the user never played.
    const auto window = profile.windowUpTo(12500);

    CHECK(window.damageDone == 2500);
    CHECK(window.casts == 25);
}

TEST_CASE("Truncation never credits the mentor past their own pull",
          "[benchmark][profile][truncation]") {
    const MentorProfile profile = makeFlatProfile(30000);

    // The user outlived the reference. The mentor's numbers stop where their
    // pull did; the window must not stretch to the user's longer duration.
    const auto window = profile.windowUpTo(90000);

    CHECK(window.durationMs == 30000);
    CHECK(window.damageDone == 6000);
}

TEST_CASE("A zero or negative cutoff yields an empty window",
          "[benchmark][profile][truncation]") {
    const MentorProfile profile = makeFlatProfile();

    CHECK(profile.windowUpTo(0).damageDone == 0);
    CHECK(profile.windowUpTo(-5000).damageDone == 0);
    CHECK(profile.windowUpTo(0).durationMs == 0);
}

TEST_CASE("Window DPS reflects the truncated span, not the full pull",
          "[benchmark][profile][truncation]") {
    const MentorProfile profile = makeFlatProfile(60000);
    const auto window = profile.windowUpTo(30000);

    // 6000 damage over 30s is 200 dps, the same rate as the full pull - a
    // flat profile should truncate to the same rate, which is the property
    // that makes short and long attempts comparable at all.
    CHECK_THAT(window.dps(),
               Catch::Matchers::WithinAbs(200.0, 0.001));
    CHECK_THAT(window.castsPerMinute(),
               Catch::Matchers::WithinAbs(120.0, 0.001));
}

TEST_CASE("Cooldown uses are cut by when they were pressed",
          "[benchmark][profile][truncation]") {
    MentorProfile profile = makeFlatProfile(60000);

    profile.cooldownUses.push_back({12345, 1, 5000, 20000, 500000, 12});
    profile.cooldownUses.push_back({12345, 2, 35000, 20000, 480000, 11});

    const auto early = profile.cooldownsUpTo(30000);
    REQUIRE(early.size() == 1);
    CHECK(early[0].useIndex == 1);

    const auto all = profile.cooldownsUpTo(60000);
    CHECK(all.size() == 2);
}

TEST_CASE("A cooldown pressed just before the cutoff still counts",
          "[benchmark][profile][truncation]") {
    MentorProfile profile = makeFlatProfile(60000);

    // Its window runs past the cutoff, but the interesting fact is that the
    // reference pressed it at all by that point.
    profile.cooldownUses.push_back({999, 1, 29000, 20000, 400000, 9});

    CHECK(profile.cooldownsUpTo(30000).size() == 1);
}

// Serialization - profiles are shared between users and outlive builds

TEST_CASE("A profile survives a JSON round trip", "[benchmark][profile][json]") {
    MentorProfile original = makeFlatProfile(45000);
    original.sourceLabel = "abc123XYZ";
    original.playerName = "Referencename";
    original.difficulty = 16;
    original.kill = true;
    original.majorCooldownSpellIds = {100, 200};
    original.unresolvedPhaseLabels = {"P3"};
    original.cooldownUses.push_back({100, 1, 6000, 20000, 750000, 15});

    ProfilePhase phase;
    phase.label = "P1";
    phase.durationMs = 45000;
    phase.totalDamageDone = 9000;
    phase.totalDamageTaken = 1800;
    phase.inCooldowns = {20000, 5000, 40, 0};
    phase.outsideCooldowns = {25000, 4000, 50, 1200};
    phase.spells.push_back({100, 2, 2.7f, 6000, true});
    phase.damageTaken.push_back({7777, 3, 1800});
    phase.damageDone.push_back({54321, 9000, 90});
    original.phases.push_back(phase);

    PhaseSettings::PhaseRule rule;
    rule.kind = PhaseSettings::RuleKind::SpellCast;
    rule.spellId = 4242;
    rule.label = "P2";
    original.phaseRules.push_back(rule);

    const auto restored = MentorProfile::fromJson(original.toJson());
    REQUIRE(restored.has_value());

    CHECK(restored->encounterId == original.encounterId);
    CHECK(restored->specId == original.specId);
    CHECK(restored->playerName == original.playerName);
    CHECK(restored->kill);
    CHECK(restored->pullDurationMs == original.pullDurationMs);
    CHECK(restored->timeline.size() == original.timeline.size());
    CHECK(restored->majorCooldownSpellIds == original.majorCooldownSpellIds);
    CHECK(restored->unresolvedPhaseLabels == original.unresolvedPhaseLabels);

    REQUIRE(restored->phaseRules.size() == 1);
    CHECK(restored->phaseRules[0].spellId == 4242);
    CHECK(restored->phaseRules[0].label == "P2");

    REQUIRE(restored->phases.size() == 1);
    CHECK(restored->phases[0].label == "P1");
    CHECK(restored->phases[0].totalDamageDone == 9000);
    CHECK(restored->phases[0].outsideCooldowns.idleMs == 1200);
    REQUIRE(restored->phases[0].spells.size() == 1);
    CHECK(restored->phases[0].spells[0].isCooldown);

    REQUIRE(restored->cooldownUses.size() == 1);
    CHECK(restored->cooldownUses[0].damageInWindow == 750000);

    // The whole point of round-tripping: a restored profile must truncate to
    // the same numbers as the original, or a shared profile would quietly
    // score differently than it did for the person who made it.
    CHECK(restored->windowUpTo(20000).damageDone ==
          original.windowUpTo(20000).damageDone);
}

TEST_CASE("Malformed or empty profile JSON is rejected, not guessed at",
          "[benchmark][profile][json]") {
    CHECK_FALSE(MentorProfile::fromJson("").has_value());
    CHECK_FALSE(MentorProfile::fromJson("not json").has_value());
    CHECK_FALSE(MentorProfile::fromJson("{}").has_value());
    CHECK_FALSE(MentorProfile::fromJson("[1,2,3]").has_value());
}

TEST_CASE("A profile from a newer build is refused rather than misread",
          "[benchmark][profile][json]") {
    // Fields may change meaning between versions. Reading it anyway would
    // produce a comparison that looks authoritative and is quietly wrong.
    const std::string future =
        R"({"version":99,"encounter":2001,"timeline":[{"t":0,"dd":1,"dt":0,"c":1}]})";

    CHECK_FALSE(MentorProfile::fromJson(future).has_value());
}

// Classification - suggestions must never decide anything

namespace {

SuggesterInput makeInput(int32_t durationMs = 300000) {
    SuggesterInput input;
    input.specId = 250;
    input.specName = "Blood";
    input.pullDurationMs = durationMs;
    return input;
}

void addCasts(SuggesterInput& input, uint32_t spellId, int32_t firstMs,
              int32_t intervalMs, int count) {
    for (int i = 0; i < count; ++i) {
        input.casts.push_back({spellId, firstMs + i * intervalMs});
    }
}

} // namespace

TEST_CASE("Nothing the suggester produces is marked confirmed",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput();
    addCasts(input, 100, 0, 120000, 3);   // Looks like a major cooldown
    addCasts(input, 200, 0, 6000, 50);    // Looks like filler
    input.auras.push_back({300, 0, 250000, true});

    const auto spec = suggestClassifications(input);

    REQUIRE_FALSE(spec.spells.empty());
    for (const auto& [id, spell] : spec.spells) {
        INFO("spell " << id);
        CHECK_FALSE(spell.confirmed);
    }
    for (const auto& [id, aura] : spec.auras) {
        INFO("aura " << id);
        CHECK_FALSE(aura.confirmed);
    }
}

TEST_CASE("A regular sub-one-per-minute cast suggests a major cooldown",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(300000);   // 5 minutes
    addCasts(input, 100, 0, 120000, 3);          // Every 2 min

    const auto spec = suggestClassifications(input);
    const auto* spell = spec.findSpell(100);

    REQUIRE(spell != nullptr);
    CHECK(spell->role == CastRole::MajorCooldown);
    CHECK(spell->basis == SuggestionBasis::CadenceMajor);
    CHECK(spell->observedCasts == 3);
}

TEST_CASE("A known long cooldown outranks its observed cadence",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(300000);
    // Only two casts, which cadence alone would treat as too few to read.
    addCasts(input, 100, 0, 180000, 2);
    input.cooldownMs[100] = 180000;

    const auto spec = suggestClassifications(input);
    const auto* spell = spec.findSpell(100);

    REQUIRE(spell != nullptr);
    CHECK(spell->role == CastRole::MajorCooldown);
    CHECK(spell->suggestionConfidence > 0.5f);
}

TEST_CASE("A frequently pressed cast suggests filler",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(300000);
    addCasts(input, 200, 0, 3000, 90);   // 18/min

    const auto spec = suggestClassifications(input);
    const auto* spell = spec.findSpell(200);

    REQUIRE(spell != nullptr);
    CHECK(spell->role == CastRole::Filler);
    CHECK(spell->basis == SuggestionBasis::CadenceFiller);
}

TEST_CASE("Irregular usage reads as situational rather than rhythmic",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(300000);
    // Four casts clustered at the start and end: the same count as a steady
    // cooldown, but no rhythm at all.
    input.casts.push_back({300, 1000});
    input.casts.push_back({300, 3000});
    input.casts.push_back({300, 280000});
    input.casts.push_back({300, 284000});

    const auto spec = suggestClassifications(input);
    const auto* spell = spec.findSpell(300);

    REQUIRE(spell != nullptr);
    CHECK(spell->basis == SuggestionBasis::Irregular);
    // Low confidence puts it near the top of the review queue, which is
    // exactly where a guess this weak belongs.
    CHECK(spell->suggestionConfidence < 0.3f);
}

TEST_CASE("A single cast is offered with low confidence",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput();
    addCasts(input, 400, 5000, 0, 1);

    const auto spec = suggestClassifications(input);
    const auto* spell = spec.findSpell(400);

    REQUIRE(spell != nullptr);
    CHECK(spell->basis == SuggestionBasis::SingleUse);
    CHECK(spell->suggestionConfidence < 0.25f);
}

TEST_CASE("A near-permanent self buff suggests maintenance",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(100000);
    input.auras.push_back({500, 0, 45000, true});
    input.auras.push_back({500, 45000, 95000, true});

    const auto spec = suggestClassifications(input);
    const auto* aura = spec.findAura(500);

    REQUIRE(aura != nullptr);
    CHECK(aura->role == AuraRole::Maintenance);
    CHECK(aura->uptimeShare > 0.9f);
}

TEST_CASE("Short repeated self buffs suggest a proc",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(100000);
    for (int i = 0; i < 6; ++i) {
        const int32_t start = i * 15000;
        input.auras.push_back({600, start, start + 4000, true});
    }

    const auto spec = suggestClassifications(input);
    const auto* aura = spec.findAura(600);

    REQUIRE(aura != nullptr);
    CHECK(aura->role == AuraRole::Proc);
}

TEST_CASE("Auras applied by someone else are left out of review",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(100000);
    input.auras.push_back({700, 0, 90000, false});   // A raid buff

    const auto spec = suggestClassifications(input);

    // Nothing the player controls, so asking them to classify it would only
    // lengthen a review list that already asks a lot.
    CHECK(spec.findAura(700) == nullptr);
}

TEST_CASE("An empty or zero-length pull suggests nothing",
          "[benchmark][classification]") {
    SuggesterInput input = makeInput(0);
    addCasts(input, 100, 0, 1000, 10);

    const auto spec = suggestClassifications(input);

    CHECK(spec.spells.empty());
    CHECK(spec.auras.empty());
}

// The store - the user's ruling has to be durable

TEST_CASE("Merging suggestions never overwrites a confirmed entry",
          "[benchmark][classification][store]") {
    SpecClassification stored;
    stored.specId = 250;
    SpellClass confirmed;
    confirmed.spellId = 100;
    confirmed.role = CastRole::Defensive;   // The user's call
    confirmed.confirmed = true;
    stored.spells[100] = confirmed;

    auto& store = ClassificationStore::instance();
    store.fromJson("");                     // Start clean
    store.mergeSuggestions(stored);

    // A later log reads the same spell as an offensive cooldown.
    SpecClassification suggestion;
    suggestion.specId = 250;
    SpellClass guess;
    guess.spellId = 100;
    guess.role = CastRole::MajorCooldown;
    guess.confirmed = false;
    guess.castsPerMinute = 0.5f;
    suggestion.spells[100] = guess;

    store.mergeSuggestions(suggestion);

    const auto* spec = store.find(250);
    REQUIRE(spec != nullptr);
    const auto* spell = spec->findSpell(100);
    REQUIRE(spell != nullptr);

    CHECK(spell->role == CastRole::Defensive);   // Ruling stands
    CHECK(spell->confirmed);
    // Evidence still refreshes, so review shows what this log actually did.
    CHECK(spell->castsPerMinute == 0.5f);
}

TEST_CASE("Genuinely new spells land in the pending queue",
          "[benchmark][classification][store]") {
    auto& store = ClassificationStore::instance();
    store.fromJson("");

    SpecClassification first;
    first.specId = 251;
    SpellClass a;
    a.spellId = 10;
    a.confirmed = true;
    first.spells[10] = a;
    store.mergeSuggestions(first);

    SpecClassification second;
    second.specId = 251;
    second.spells[10] = a;
    SpellClass b;
    b.spellId = 20;      // New this log
    b.confirmed = false;
    second.spells[20] = b;

    const size_t added = store.mergeSuggestions(second);

    CHECK(added == 1);
    const auto* spec = store.find(251);
    REQUIRE(spec != nullptr);
    CHECK_FALSE(spec->fullyConfirmed());
    REQUIRE(spec->pendingSpellIds.size() == 1);
    CHECK(spec->pendingSpellIds[0] == 20);
}

TEST_CASE("The pending queue puts the weakest suggestions first",
          "[benchmark][classification][store]") {
    auto& store = ClassificationStore::instance();
    store.fromJson("");

    SpecClassification spec;
    spec.specId = 252;
    for (uint32_t i = 1; i <= 3; ++i) {
        SpellClass s;
        s.spellId = i * 10;
        s.confirmed = false;
        s.suggestionConfidence = 0.1f * i;   // 0.1, 0.2, 0.3
        spec.spells[s.spellId] = s;
    }
    store.mergeSuggestions(spec);

    const auto* stored = store.find(252);
    REQUIRE(stored != nullptr);
    REQUIRE(stored->pendingSpellIds.size() == 3);
    CHECK(stored->pendingSpellIds[0] == 10);   // Least confident
    CHECK(stored->pendingSpellIds[2] == 30);
}

TEST_CASE("Only confirmed major cooldowns define the burst window",
          "[benchmark][classification][store]") {
    SpecClassification spec;
    spec.specId = 253;

    SpellClass confirmedMajor;
    confirmedMajor.spellId = 100;
    confirmedMajor.role = CastRole::MajorCooldown;
    confirmedMajor.confirmed = true;
    spec.spells[100] = confirmedMajor;

    SpellClass suggestedMajor;
    suggestedMajor.spellId = 200;
    suggestedMajor.role = CastRole::MajorCooldown;
    suggestedMajor.confirmed = false;    // Only a guess so far
    spec.spells[200] = suggestedMajor;

    const auto ids = spec.majorCooldownSpellIds();

    // Letting an unconfirmed guess define burst would reshape every window in
    // the comparison off something nobody agreed to.
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 100);
}

TEST_CASE("Confirming a spell records the role it was confirmed as",
          "[benchmark][classification][store]") {
    auto& store = ClassificationStore::instance();
    store.fromJson("");

    SpecClassification spec;
    spec.specId = 254;
    SpellClass s;
    s.spellId = 100;
    s.role = CastRole::MajorCooldown;   // Suggested
    s.confirmed = false;
    spec.spells[100] = s;
    store.mergeSuggestions(spec);

    // The user corrects it rather than accepting.
    store.confirmSpell(254, 100, CastRole::Defensive);

    const auto* stored = store.find(254);
    REQUIRE(stored != nullptr);
    const auto* spell = stored->findSpell(100);
    REQUIRE(spell != nullptr);
    CHECK(spell->role == CastRole::Defensive);
    CHECK(spell->confirmed);
    CHECK(stored->fullyConfirmed());
}

TEST_CASE("The classification store survives a JSON round trip",
          "[benchmark][classification][store]") {
    auto& store = ClassificationStore::instance();
    store.fromJson("");

    SpecClassification spec;
    spec.specId = 255;
    spec.specName = "Frost";
    spec.gameVersion = "12.1";

    SpellClass s;
    s.spellId = 100;
    s.name = "Reference Ability";
    s.role = CastRole::MajorCooldown;
    s.confirmed = true;
    s.castsPerMinute = 0.5f;
    s.observedCasts = 3;
    s.medianGapMs = 120000;
    s.windowAuraSpellId = 100;
    spec.spells[100] = s;

    AuraClass a;
    a.spellId = 300;
    a.role = AuraRole::Proc;
    a.confirmed = true;
    a.applications = 7;
    a.consumedBySpellId = 200;
    spec.auras[300] = a;

    store.mergeSuggestions(spec);
    const std::string json = store.toJson();

    store.fromJson("");
    REQUIRE(store.find(255) == nullptr);

    store.fromJson(json);
    const auto* restored = store.find(255);
    REQUIRE(restored != nullptr);
    CHECK(restored->specName == "Frost");
    CHECK(restored->gameVersion == "12.1");

    const auto* spell = restored->findSpell(100);
    REQUIRE(spell != nullptr);
    CHECK(spell->role == CastRole::MajorCooldown);
    CHECK(spell->confirmed);
    CHECK(spell->windowAuraSpellId == 100);
    CHECK(spell->observedCasts == 3);

    const auto* aura = restored->findAura(300);
    REQUIRE(aura != nullptr);
    CHECK(aura->role == AuraRole::Proc);
    CHECK(aura->consumedBySpellId == 200);

    CHECK(restored->fullyConfirmed());
}

TEST_CASE("Corrupt store JSON leaves an empty store rather than a half-read one",
          "[benchmark][classification][store]") {
    auto& store = ClassificationStore::instance();
    store.fromJson("{\"broken\":");

    CHECK(store.find(250) == nullptr);
}

// Spec resolution from imported report names

TEST_CASE("Spec ids resolve from the class and spec names reports carry",
          "[benchmark][spec]") {
    // Imported reports have no COMBATANT_INFO, so the actor list's
    // "DemonHunter" + icon "DemonHunter-Devourer" is the only spec source.
    CHECK(parser::specIdFromNames("DemonHunter", "Devourer") == 1480);
    CHECK(parser::specIdFromNames("DemonHunter", "Havoc") == 577);
    CHECK(parser::specIdFromNames("DeathKnight", "Blood") == 250);
    CHECK(parser::specIdFromNames("Hunter", "BeastMastery") == 253);
}

TEST_CASE("Spec name matching ignores spacing differences",
          "[benchmark][spec]") {
    // Reports send names without spaces while the tables hold them with.
    CHECK(parser::specIdFromNames("Death Knight", "Blood") == 250);
    CHECK(parser::specIdFromNames("Hunter", "Beast Mastery") == 253);
    CHECK(parser::specIdFromNames("hunter", "beast mastery") == 253);
}

TEST_CASE("An unresolvable spec returns zero rather than a wrong id",
          "[benchmark][spec]") {
    CHECK(parser::specIdFromNames("DemonHunter", "") == 0);
    CHECK(parser::specIdFromNames("DemonHunter", "NotASpec") == 0);
    // Right spec name, wrong class: better to resolve nothing than to
    // classify a player as a spec they are not.
    CHECK(parser::specIdFromNames("Warrior", "Devourer") == 0);
}

// Ignored spells - a ruling of "this does not matter" has to reach every number

TEST_CASE("Only confirmed rulings mark a spell ignored",
          "[benchmark][classification][ignored]") {
    SpecClassification spec;
    spec.specId = 260;

    SpellClass confirmed;
    confirmed.spellId = 100;
    confirmed.role = CastRole::Ignored;
    confirmed.confirmed = true;
    spec.spells[100] = confirmed;

    SpellClass suggested;
    suggested.spellId = 200;
    suggested.role = CastRole::Ignored;
    suggested.confirmed = false;   // Still only a guess
    spec.spells[200] = suggested;

    const auto ids = spec.ignoredSpellIds();

    // Dropping a spell from every metric on an unconfirmed guess would remove
    // it from the report without the user ever agreeing to that.
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 100);
}

TEST_CASE("Only confirmed rulings mark an aura unimportant",
          "[benchmark][classification][ignored]") {
    SpecClassification spec;
    spec.specId = 260;

    AuraClass confirmed;
    confirmed.spellId = 300;
    confirmed.role = AuraRole::Unimportant;
    confirmed.confirmed = true;
    spec.auras[300] = confirmed;

    AuraClass suggested;
    suggested.spellId = 400;
    suggested.role = AuraRole::Unimportant;
    suggested.confirmed = false;
    spec.auras[400] = suggested;

    const auto ids = spec.ignoredAuraIds();

    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 300);
}

TEST_CASE("An ignored spell is left out of the cadence suggestions",
          "[benchmark][classification][ignored]") {
    SuggesterInput input;
    input.specId = 260;
    input.pullDurationMs = 300000;

    // A spell cast constantly, which would otherwise dominate the filler band.
    for (int i = 0; i < 90; ++i) {
        input.casts.push_back({100, i * 3000});
    }
    for (int i = 0; i < 10; ++i) {
        input.casts.push_back({200, i * 30000});
    }

    const auto withBoth = suggestClassifications(input);
    REQUIRE(withBoth.findSpell(100) != nullptr);

    // Now drop 100 the way buildSuggesterInput would once it is ignored.
    SuggesterInput filtered = input;
    filtered.casts.erase(
        std::remove_if(filtered.casts.begin(), filtered.casts.end(),
                       [](const SuggesterCast& c) { return c.spellId == 100; }),
        filtered.casts.end());

    const auto withoutIgnored = suggestClassifications(filtered);

    // Gone from the queue entirely - not re-suggested for the user to rule on
    // a second time.
    CHECK(withoutIgnored.findSpell(100) == nullptr);
    CHECK(withoutIgnored.findSpell(200) != nullptr);
}

TEST_CASE("A profile records which spells it excluded",
          "[benchmark][profile][ignored]") {
    MentorProfile profile = makeFlatProfile(60000);
    profile.majorCooldownSpellIds = {100};
    profile.ignoredSpellIds = {700, 800};

    const auto restored = MentorProfile::fromJson(profile.toJson());
    REQUIRE(restored.has_value());

    // Carried through the round trip for the same reason the cooldowns are: a
    // profile cut under a different exclusion set is not comparable with a
    // fresh one, and the difference has to be detectable rather than silent.
    CHECK(restored->ignoredSpellIds == profile.ignoredSpellIds);
}

TEST_CASE("A cast and buff sharing an id define the cooldown window",
          "[benchmark][classification][window]") {
    SpecClassification spec;
    spec.specId = 577;

    // Void Metamorphosis: you press the ability to enter the window, and the
    // window runs until the buff it grants drops. Both sides carry the same
    // spell id, which is the whole signal.
    SpellClass cast;
    cast.spellId = 473728;
    cast.role = CastRole::MajorCooldown;
    cast.confirmed = true;
    spec.spells[473728] = cast;

    AuraClass buff;
    buff.spellId = 473728;
    buff.role = AuraRole::CooldownBuff;
    buff.confirmed = true;
    spec.auras[473728] = buff;

    SECTION("the pair is found without anyone linking it by hand") {
        REQUIRE(spec.windowAuraFor(473728) == 473728);
    }

    SECTION("a cast with no matching buff has no window aura") {
        SpellClass lone;
        lone.spellId = 12345;
        lone.role = CastRole::MajorCooldown;
        lone.confirmed = true;
        spec.spells[12345] = lone;
        REQUIRE(spec.windowAuraFor(12345) == 0);
    }

    SECTION("an explicit link wins over the shared id") {
        spec.spells[473728].windowAuraSpellId = 999;
        REQUIRE(spec.windowAuraFor(473728) == 999);
    }

    SECTION("the pair holds when the role is corrected after suggestion") {
        // The regression this guards. The link used to be resolved once while
        // suggesting, and only for spells already guessed to be major
        // cooldowns - so a spell the user promoted to one afterwards silently
        // kept no window at all.
        spec.spells[473728].role = CastRole::Filler;
        spec.spells[473728].role = CastRole::MajorCooldown;
        REQUIRE(spec.windowAuraFor(473728) == 473728);
    }
}

TEST_CASE("Trinkets and cooldowns stacked together form one burst window",
          "[benchmark][classification][window]") {
    SpecClassification spec;
    spec.specId = 577;

    SpellClass meta;
    meta.spellId = 500;
    meta.role = CastRole::MajorCooldown;
    meta.confirmed = true;
    spec.spells[500] = meta;

    SpellClass trinket;
    trinket.spellId = 600;
    trinket.role = CastRole::Trinket;
    trinket.confirmed = true;
    spec.spells[600] = trinket;

    SpellClass filler;
    filler.spellId = 700;
    filler.role = CastRole::Filler;
    filler.confirmed = true;
    spec.spells[700] = filler;

    SECTION("a trinket opens a burst window, a filler does not") {
        const auto ids = spec.burstWindowSpellIds();
        REQUIRE(ids.size() == 2);
        REQUIRE(std::find(ids.begin(), ids.end(), 500u) != ids.end());
        REQUIRE(std::find(ids.begin(), ids.end(), 600u) != ids.end());
        REQUIRE(std::find(ids.begin(), ids.end(), 700u) == ids.end());
    }

    SECTION("the stored fingerprint stays only the major cooldowns") {
        // Widening this would make every profile cut before trinkets opened
        // windows look stale and demand a rebuild it does not need.
        const auto fingerprint = spec.majorCooldownSpellIds();
        REQUIRE(fingerprint.size() == 1);
        REQUIRE(fingerprint.front() == 500u);
    }

    SECTION("an unconfirmed trinket does not open a window") {
        spec.spells[600].confirmed = false;
        const auto ids = spec.burstWindowSpellIds();
        REQUIRE(std::find(ids.begin(), ids.end(), 600u) == ids.end());
    }
}

TEST_CASE("A buff with no cast can still be a major cooldown",
          "[benchmark][classification][window]") {
    SpecClassification spec;
    spec.specId = 577;

    // Some abilities never appear as a cast at all - the log's first sight of
    // them is the buff being gained. Without this they cannot be marked and
    // the whole pull reads as filler.
    AuraClass buffOnly;
    buffOnly.spellId = 1242504;
    buffOnly.role = AuraRole::MajorCooldown;
    buffOnly.confirmed = true;
    spec.auras[1242504] = buffOnly;

    SECTION("the buff is offered as a window source") {
        const auto ids = spec.buffOnlyCooldownIds();
        REQUIRE(ids.size() == 1);
        REQUIRE(ids.front() == 1242504u);
    }

    SECTION("a cast of the same id takes precedence") {
        // Otherwise the cast and the buff would each open a window and the
        // same burst would be counted twice.
        SpellClass cast;
        cast.spellId = 1242504;
        cast.role = CastRole::MajorCooldown;
        cast.confirmed = true;
        spec.spells[1242504] = cast;
        REQUIRE(spec.buffOnlyCooldownIds().empty());
    }

    SECTION("an unconfirmed ruling is not used") {
        spec.auras[1242504].confirmed = false;
        REQUIRE(spec.buffOnlyCooldownIds().empty());
    }
}

TEST_CASE("Buff-only and trinket rulings are visible to the rebuild check",
          "[benchmark][classification][window]") {
    // The regression: ruling a buff a major cooldown changed where the burst
    // windows are, but nothing compared that against the profile - so no
    // re-extraction happened and the ruling silently never took effect. The
    // profile has to carry both sets for the change to be detectable at all.
    SpecClassification spec;
    spec.specId = 577;

    AuraClass window;
    window.spellId = 1242504;
    window.role = AuraRole::MajorCooldown;
    window.confirmed = true;
    spec.auras[1242504] = window;

    SpellClass trinket;
    trinket.spellId = 600;
    trinket.role = CastRole::Trinket;
    trinket.confirmed = true;
    spec.spells[600] = trinket;

    MentorProfile stale;
    stale.encounterId = 3009;
    stale.specId = 577;
    stale.timeline.push_back({});
    // Cut before either ruling existed.
    REQUIRE(stale.burstWindowSpellIds.empty());
    REQUIRE(stale.buffOnlyCooldownIds.empty());

    REQUIRE(spec.buffOnlyCooldownIds() != stale.buffOnlyCooldownIds);
    REQUIRE(spec.burstWindowSpellIds() != stale.burstWindowSpellIds);

    SECTION("a profile cut under the same rulings compares equal") {
        stale.burstWindowSpellIds = spec.burstWindowSpellIds();
        stale.buffOnlyCooldownIds = spec.buffOnlyCooldownIds();
        REQUIRE(spec.burstWindowSpellIds() == stale.burstWindowSpellIds);
        REQUIRE(spec.buffOnlyCooldownIds() == stale.buffOnlyCooldownIds);
    }

    SECTION("both sets survive a JSON round trip") {
        stale.burstWindowSpellIds = spec.burstWindowSpellIds();
        stale.buffOnlyCooldownIds = spec.buffOnlyCooldownIds();
        const auto restored = MentorProfile::fromJson(stale.toJson());
        REQUIRE(restored.has_value());
        REQUIRE(restored->burstWindowSpellIds == stale.burstWindowSpellIds);
        REQUIRE(restored->buffOnlyCooldownIds == stale.buffOnlyCooldownIds);
    }
}

TEST_CASE("A cast occupies the player for its global, not just its instant",
          "[benchmark][extractor][idle]") {
    // The regression this guards: idle time was measured between cast END
    // timestamps with no account of the global cooldown or the cast duration,
    // so a pull spent casting reported as roughly half idle.
    SpellCastEvent instant;
    instant.spell_id = 100;
    instant.start_timestamp_ms = 0;
    instant.end_timestamp_ms = 0;
    instant.haste_permille = 0;

    SECTION("an instant at no haste occupies the base global") {
        REQUIRE(benchmark::castOccupancyMs(&instant) == 1500);
    }

    SECTION("haste shortens the global") {
        instant.haste_permille = 500;   // 50% haste
        REQUIRE(benchmark::castOccupancyMs(&instant) == 1000);
    }

    SECTION("the global never falls below the floor") {
        // Blizzard clamps at 750ms however much haste is stacked, and reading
        // below it would invent idle time between fast presses.
        instant.haste_permille = 5000;
        REQUIRE(benchmark::castOccupancyMs(&instant) == 750);
    }

    SECTION("a cast longer than the global occupies the cast") {
        // The two overlap rather than adding: a three second cast costs three
        // seconds, not three plus a global.
        SpellCastEvent hardcast;
        hardcast.spell_id = 200;
        hardcast.start_timestamp_ms = 1000;
        hardcast.end_timestamp_ms = 4000;
        hardcast.haste_permille = 0;
        REQUIRE(hardcast.cast_duration_ms() == 3000);
        REQUIRE(benchmark::castOccupancyMs(&hardcast) == 3000);
    }
}

TEST_CASE("Blacklisting an aura the suggester never offered",
          "[benchmark][classification][blacklist]") {
    // Two bugs met here. Most buffs in a cooldown window come from other
    // players, and the suggester only ever offers self-applied ones - so
    // confirmAura found nothing to update and the blacklist did nothing.
    // Then the ruling that did land was never written to disk, because the
    // save lived in the review panel rather than the store.
    // The store is a singleton, so each section starts from a known empty
    // state rather than inheriting whatever a previous one ruled.
    auto& store = ClassificationStore::instance();
    store.fromJson("");

    SpecClassification spec;
    spec.specId = 1480;
    spec.specName = "Devourer";
    store.mergeSuggestions(spec);

    constexpr uint32_t kExternalBuff = 395152;   // Ebon Might, cast by an Augmentation
    REQUIRE(store.find(1480) != nullptr);
    REQUIRE(store.find(1480)->findAura(kExternalBuff) == nullptr);

    store.ruleUnlistedAura(1480, kExternalBuff, AuraRole::Unimportant);

    SECTION("the ruling creates the entry") {
        const auto* aura = store.find(1480)->findAura(kExternalBuff);
        REQUIRE(aura != nullptr);
        REQUIRE(aura->confirmed);
        REQUIRE(aura->role == AuraRole::Unimportant);
    }

    SECTION("it reaches the ignored list the extractor reads") {
        const auto ignored = store.find(1480)->ignoredAuraIds();
        REQUIRE(std::find(ignored.begin(), ignored.end(), kExternalBuff) !=
                ignored.end());
    }

    SECTION("it survives a save and reload") {
        const std::string json = store.toJson();
        store.fromJson("");
        REQUIRE(store.find(1480) == nullptr);

        store.fromJson(json);
        const auto* aura = store.find(1480)->findAura(kExternalBuff);
        REQUIRE(aura != nullptr);
        REQUIRE(aura->role == AuraRole::Unimportant);
    }
}

// Hostility from unit flags
// Two features read this: the phase editor's boss-cast list and the mob
// weight list. Both were showing player pets before it existed, so the cases
// below are the ones that actually went wrong rather than a sweep of the enum.

namespace {

ActorEvent actorWith(std::string_view guid, UnitReaction reaction,
                     UnitController controller) {
    ActorEvent event;
    event.source_guid = guid;
    event.flags.reaction = reaction;
    event.flags.controller = controller;
    return event;
}

} // namespace

TEST_CASE("Hostile actors are read from unit flags", "[hostility]") {
    ExtractedData data;

    SECTION("an npc fighting the raid is hostile") {
        data.actorEvents.push_back(
            actorWith("Creature-0-1-2-3-12345-000", UnitReaction::Hostile,
                      UnitController::NPC));

        const auto hostile = data.hostileActorGuids();
        CHECK(hostile.count("Creature-0-1-2-3-12345-000") == 1);
    }

    SECTION("a player is not") {
        data.actorEvents.push_back(
            actorWith("Player-1-ABCDEF", UnitReaction::Friendly,
                      UnitController::Player));

        CHECK(data.hostileActorGuids().empty());
    }

    SECTION("a player summon keeps its Creature GUID but is still not a mob") {
        // An army of ghouls, a Magus of the Dead: Creature- GUID, npc id and
        // all, which is exactly why the GUID prefix was not enough.
        data.actorEvents.push_back(
            actorWith("Creature-0-1-2-3-24207-000", UnitReaction::Friendly,
                      UnitController::Player));

        CHECK(data.hostileActorGuids().empty());
    }

    SECTION("a charmed player counts as hostile while controlled") {
        // Hostile reaction, but still player-controlled - the controller
        // check is what decides it, and it keeps this out.
        data.actorEvents.push_back(
            actorWith("Player-1-FEDCBA", UnitReaction::Hostile,
                      UnitController::Player));

        CHECK(data.hostileActorGuids().empty());
    }

    SECTION("resource events contribute too") {
        // Health updates are often all a boss produces before its first cast.
        ResourceEvent res;
        res.actor_guid = "Creature-0-1-2-3-99999-000";
        res.flags.reaction = UnitReaction::Hostile;
        res.flags.controller = UnitController::NPC;
        data.resourceEvents.push_back(res);

        CHECK(data.hostileActorGuids().count("Creature-0-1-2-3-99999-000") == 1);
    }

    SECTION("an empty guid is never listed") {
        data.actorEvents.push_back(
            actorWith("", UnitReaction::Hostile, UnitController::NPC));

        CHECK(data.hostileActorGuids().empty());
    }
}

// npc ids out of imported GUIDs
// An imported report's GUIDs are synthetic: field 6 is the report's own actor
// index, not a creature id. Reading it straight out of the GUID folds
// unrelated mobs onto whatever indices happen to collide with real ids, which
// is why the mob weight list showed a handful of rows for a full pull.

TEST_CASE("npcIdFromGuid resolves imported actor ids", "[mobweight][npcid]") {
    SECTION("a local log reads the id straight from the GUID") {
        const std::unordered_map<int32_t, uint32_t> noSideMap;
        CHECK(MobWeightSettings::npcIdFromGuid(
                  "Creature-0-3018-2810-26542-241517-00003256C6", noSideMap) == 241517);
    }

    SECTION("an import resolves through the side map") {
        // Actor 16 in this report is creature 241517. The GUID says 16.
        const std::unordered_map<int32_t, uint32_t> sideMap{{16, 241517}, {17, 242587}};
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-16-3", sideMap) == 241517);
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-17-9", sideMap) == 242587);
    }

    SECTION("two report actors stay distinct") {
        // The bug this guards: without the side map both of these read as
        // their actor index, and distinct mobs merged into one row.
        const std::unordered_map<int32_t, uint32_t> sideMap{{16, 241517}, {17, 242587}};
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-16-3", sideMap) !=
              MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-17-9", sideMap));
    }

    SECTION("an actor missing from the side map is not guessed at") {
        const std::unordered_map<int32_t, uint32_t> sideMap{{16, 241517}};
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-99-1", sideMap) == 0);
    }

    SECTION("players never carry an npc id") {
        const std::unordered_map<int32_t, uint32_t> sideMap{{16, 241517}};
        CHECK(MobWeightSettings::npcIdFromGuid("Player-11-0E8ACC6C", sideMap) == 0);
    }
}

TEST_CASE("Imported GUIDs match between combat and position events",
          "[mobweight][npcid][import]") {
    // WarcraftLogs position and resource events carry a spawn instance in the
    // GUID ("Creature-0-0-0-0-16-3") while the combat events for the same mob
    // do not ("Creature-0-0-0-0-16"). Anything that builds a set from one and
    // looks up the other silently matches almost nothing.
    const std::unordered_map<int32_t, uint32_t> sideMap{{16, 241517}};

    SECTION("both GUID shapes resolve to the same creature") {
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-16", sideMap) == 241517);
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-16-3", sideMap) == 241517);
    }

    SECTION("every spawn of one mob folds onto one row") {
        CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-16-1", sideMap) ==
              MobWeightSettings::npcIdFromGuid("Creature-0-0-0-0-16-7", sideMap));
    }
}

TEST_CASE("Hostility survives the import's two GUID shapes",
          "[mobweight][npcid][import]") {
    // The bug this guards. An import records a mob's position and health with
    // the spawn instance in the GUID, but its damage and cast events without
    // it. Hostility was collected from the former and looked up with the
    // latter, so almost every enemy failed the check and the mob weight list
    // showed two rows for a full pull.
    ExtractedData data;
    data.actorIdToNpcId = {{16, 241517}, {17, 242587}};

    // Three spawns of two mobs, as the position stream writes them.
    for (const auto* guid : {"Creature-0-0-0-0-16-1",
                             "Creature-0-0-0-0-16-2",
                             "Creature-0-0-0-0-17-1"}) {
        ResourceEvent res;
        res.actor_guid = guid;
        res.flags.reaction = UnitReaction::Hostile;
        res.flags.controller = UnitController::NPC;
        data.resourceEvents.push_back(res);
    }

    const auto hostileIds = data.hostileNpcIds();

    // Two distinct creatures, not three spawns and not two report indices.
    CHECK(hostileIds.size() == 2);
    CHECK(hostileIds.count(241517) == 1);
    CHECK(hostileIds.count(242587) == 1);

    // What a damage event carries, with no instance suffix, still resolves
    // to a creature that the position stream declared hostile.
    CHECK(hostileIds.count(data.npcIdOf("Creature-0-0-0-0-16")) == 1);
    CHECK(hostileIds.count(data.npcIdOf("Creature-0-0-0-0-17")) == 1);

    // The GUID sets themselves genuinely disagree - this is why keying on
    // the GUID could not work.
    const auto hostileGuidSet = data.hostileActorGuids();
    CHECK(hostileGuidSet.count("Creature-0-0-0-0-16") == 0);
}

TEST_CASE("readRoster finds the raid without parsing the pull",
          "[benchmark][roster]") {
    // Filling a name dropdown used to run the whole pull through the event
    // extractor - hundreds of thousands of events and a hundred-odd megabytes
    // to read twenty lines. This scans the head of the range instead.
    const std::filesystem::path log = "testevents.txt";
    if (!std::filesystem::exists(log)) {
        WARN("testevents.txt missing; skipping roster scan");
        return;
    }

    const auto bytes = std::filesystem::file_size(log);
    const auto roster = benchmark::readRoster(log, 0, bytes);

    // Whatever it finds must be usable: a name to show and a guid to match.
    for (const auto& entry : roster) {
        CHECK_FALSE(entry.name.empty());
        CHECK_FALSE(entry.guid.empty());
        CHECK(entry.guid.rfind("Player-", 0) == 0);
    }

    // Sorted by name, which is the order the picker shows.
    for (size_t i = 1; i < roster.size(); ++i) {
        CHECK(roster[i - 1].name <= roster[i].name);
    }

    // Same players the full parse would have reported, so the cheap path did
    // not quietly lose anyone.
    benchmark::LivePullEvents pull;
    benchmark::extractLivePull(pull, log, 0, bytes);
    if (pull.valid) {
        // By unique guid: a log spanning several pulls repeats COMBATANT_INFO
        // for the same player, and the picker must list them once.
        std::set<std::string> namedCombatants;
        for (const auto& info : pull.data.combatantInfoEvents) {
            auto it = pull.data.guidToNameMap.find(info.player_guid);
            if (it != pull.data.guidToNameMap.end() && !it->second.empty()) {
                namedCombatants.insert(info.player_guid);
            }
        }
        CHECK(roster.size() == namedCombatants.size());

        // And the same people, not merely the same count.
        for (const auto& entry : roster) {
            CHECK(namedCombatants.contains(entry.guid));
        }
    }
}

TEST_CASE("A player who appears in no event is credited with no damage",
          "[benchmark][extract]") {
    // The guid is never interned, so it looks up as INVALID (0). Ownerless
    // events carry owner 0 too, and must not be read as this player's.
    ExtractedData data;
    CombatEvent hit{};
    hit.source_guid_id = guidInterner().intern("Creature-0-1-2-3-200000-00EXTRREG1");
    hit.target_guid_id = guidInterner().intern("Player-1-EXTRREGTARGET");
    hit.owner_guid_id = StringInterner::INVALID;
    hit.timestamp_ms = 5000;
    hit.amount = 400000;
    hit.effective_amount = 400000;
    hit.event_type = CombatEventType::DamageDealt;
    data.combatEvents.push_back(hit);

    ExtractionRequest request;
    request.actorGuid = "Player-1-EXTRREGNEVERSEEN";
    request.encounterId = 3009;
    const MentorProfile profile = extractProfile(data, request);

    int64_t damage = 0;
    for (const auto& bucket : profile.timeline) damage += bucket.damageDone;
    CHECK(damage == 0);
}
