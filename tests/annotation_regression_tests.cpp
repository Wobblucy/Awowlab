// Regression tests for trigger and annotation files. These are shared between
// players, so the loaders have to cope with whatever is in them.
#include <catch2/catch_all.hpp>
#include "AutoAnnotationTriggerManager.h"
#include "AutoAnnotationTypes.h"
#include "AutoAnnotationWizardState.h"
#include "UIAutoAnnotationPanel.h"
#include "../AwowLab/AnnotationManager/src/AnnotationSerializer.h"
#include "../AwowLab/Core/CurrentEncounter.h"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("A trail interval of zero is clamped on load", "[annotation][trigger]") {
    // The trail loop steps by this interval, so zero never finishes. Snapshot
    // imports come through this same reader.
    AutoAnnotationTriggerManager manager;
    REQUIRE(manager.deserializeTriggersFromJson(R"({"version":1,"triggers":[
        {"category":"Actor","actorNameFilter":"Void Orb","timing":"ActorTrail","trailIntervalMs":0},
        {"category":"Actor","actorNameFilter":"Void Orb","timing":"ActorTrail","trailIntervalMs":-500}
    ]})"));

    REQUIRE(manager.getTriggers().size() == 2);
    for (const auto& trigger : manager.getTriggers()) {
        CHECK(trigger.trailIntervalMs >= kMinTrailIntervalMs);
    }
}

TEST_CASE("A trigger file cannot claim more stack colours than there are",
          "[annotation][trigger]") {
    // stackColors holds four entries. A count of 200 used to sort 200 of them.
    const std::string encounter = "AwowLab Stack Colour Test";
    std::filesystem::create_directories("triggers");
    const std::filesystem::path file = std::filesystem::path("triggers") / (encounter + ".json");
    {
        std::ofstream out(file, std::ios::binary);
        out << R"({"version":1,"triggers":[{"spell_id":1234,"timing":"OnDebuffApplied",
            "stackColorCount":200,"stackColors":[{"stackCount":1},{"stackCount":3},5]}]})";
    }

    CurrentEncounter::instance().set(encounter);
    AutoAnnotationTriggerManager manager;
    manager.loadFromFile();
    CurrentEncounter::instance().clear();
    std::filesystem::remove(file);
    std::error_code ec;
    std::filesystem::remove("triggers", ec);  // fails harmlessly unless empty

    REQUIRE(manager.getTriggers().size() == 1);
    const auto& trigger = manager.getTriggers()[0];
    CHECK(trigger.stackColorCount <= trigger.stackColors.size());
    CHECK(trigger.stackColors[0].stackCount == 3);
}

TEST_CASE("On Cast Start timing and the Source to Target anchor survive a save",
          "[annotation][trigger]") {
    AutoAnnotationTrigger trigger;
    trigger.id = 1;
    trigger.spell_id = 4321;
    trigger.timing = TriggerTiming::OnCastStart;
    trigger.anchor = TriggerAnchor::SourceToTarget;

    const std::string json =
        AutoAnnotationTriggerManager::serializeTriggersToJson({trigger}, nullptr);
    AutoAnnotationTriggerManager manager;
    REQUIRE(manager.deserializeTriggersFromJson(json));

    REQUIRE(manager.getTriggers().size() == 1);
    CHECK(manager.getTriggers()[0].timing == TriggerTiming::OnCastStart);
    CHECK(manager.getTriggers()[0].anchor == TriggerAnchor::SourceToTarget);
}

namespace {

// Every field away from its default, so a reader that drops one shows up
AutoAnnotationTrigger makeFullyConfiguredTrigger() {
    AutoAnnotationTrigger trigger;
    trigger.id = 1;
    trigger.category = TriggerCategory::Spell;
    trigger.spell_id = 98765;
    trigger.creature_guid = "Creature-0-1-2-3-4-5";
    trigger.creature_name = "Test Boss";
    trigger.sourceFilter = SourceFilter::EnvironmentOnly;
    trigger.actorNameFilter = "Void Orb";
    trigger.trailIntervalMs = 750;
    trigger.deathBehavior = ActorDeathBehavior::FadeOverTime;
    trigger.timing = TriggerTiming::Tether;
    trigger.anchor = TriggerAnchor::CasterFacing;
    trigger.shape = AnnotationShape::Donut;
    trigger.color = {0.25f, 0.5f, 0.75f, 0.125f};
    trigger.dimensions = {12.5f, 3.0f, 45.0f, 0.5f};
    trigger.pointCount = 9;
    trigger.visualStyle = AnnotationVisualStyle::RippleRings;
    trigger.stripeSpacing = 4.5f;
    trigger.rippleSpeed = 7.25f;
    trigger.rippleCount = 2.0f;
    trigger.enableEdgeFade = true;
    trigger.enableGlow = true;
    trigger.enableOutline = true;
    trigger.enableGradient = true;
    trigger.edgeFadeWidth = 0.25f;
    trigger.outlineWidth = 0.125f;
    trigger.glowIntensity = 1.5f;
    trigger.useSecondaryColor = true;
    trigger.useInnerZoneColor = true;
    trigger.secondaryColor = {0.125f, 0.25f, 0.375f, 0.5f};
    trigger.innerZoneColor = {0.5f, 0.625f, 0.75f, 0.875f};
    trigger.durationType = AnnotationDuration::Encounter;
    trigger.durationMs = 4500;
    trigger.leadTimeMs = 1500;
    trigger.castExtendMs = 2500;
    trigger.offsetX = -3.5f;
    trigger.offsetY = 6.25f;
    trigger.minStacks = 2;
    trigger.maxStacks = 8;
    trigger.stackColorCount = 2;
    trigger.stackColors[0] = {5, {1.0f, 0.0f, 0.0f, 1.0f}};
    trigger.stackColors[1] = {3, {0.0f, 1.0f, 0.0f, 1.0f}};
    trigger.motion.type = AnnotationMotionType::Projectile;
    trigger.motion.rotationSpeed_degPerSec = 90.0f;
    trigger.motion.endOffsetX = 10.0f;
    trigger.motion.endOffsetY = -10.0f;
    trigger.motion.travelTime_ms = 1250;
    trigger.motion.endBehavior = ProjectileEndBehavior::Disappear;
    trigger.motion.endMode = ProjectileEndMode::ToTarget;
    trigger.motion.projectileDistance = 22.5f;
    trigger.motion.facingOffset_deg = 15.0f;
    trigger.motion.returnToOrigin = true;
    trigger.motion.dropsPerSecond = 3.0f;
    trigger.motion.dropRadius = 2.5f;
    trigger.motion.dropLifetime_ms = 6000;
    trigger.enabled = false;
    trigger.ignoreEarlyPullCasts = true;
    trigger.isNegation = true;
    trigger.negatesSpellIds = {111, 222};
    return trigger;
}

} // namespace

TEST_CASE("Every trigger field survives a snapshot round trip", "[annotation][trigger]") {
    const AutoAnnotationTrigger original = makeFullyConfiguredTrigger();
    const std::string json =
        AutoAnnotationTriggerManager::serializeTriggersToJson({original}, nullptr);

    AutoAnnotationTriggerManager manager;
    REQUIRE(manager.deserializeTriggersFromJson(json));
    REQUIRE(manager.getTriggers().size() == 1);
    CHECK(AutoAnnotationTriggerManager::serializeTriggersToJson(manager.getTriggers(), nullptr) == json);
}

TEST_CASE("Every trigger field survives a save to the trigger folder", "[annotation][trigger]") {
    const std::string encounter = "AwowLab Round Trip Test";
    const AutoAnnotationTrigger original = makeFullyConfiguredTrigger();
    const std::string json =
        AutoAnnotationTriggerManager::serializeTriggersToJson({original}, nullptr, encounter);

    std::filesystem::create_directories("triggers");
    const std::filesystem::path file = std::filesystem::path("triggers") / (encounter + ".json");
    {
        std::ofstream out(file, std::ios::binary);
        out << json;
    }

    CurrentEncounter::instance().set(encounter);
    AutoAnnotationTriggerManager manager;
    manager.loadFromFile();
    CurrentEncounter::instance().clear();
    std::filesystem::remove(file);
    std::error_code ec;
    std::filesystem::remove("triggers", ec);

    REQUIRE(manager.getTriggers().size() == 1);
    CHECK(AutoAnnotationTriggerManager::serializeTriggersToJson(manager.getTriggers(), nullptr, encounter) == json);
}

TEST_CASE("Trigger files keep reading the older field spellings", "[annotation][trigger]") {
    const std::string encounter = "AwowLab Legacy Field Test";
    std::filesystem::create_directories("triggers");
    const std::filesystem::path file = std::filesystem::path("triggers") / (encounter + ".json");
    {
        std::ofstream out(file, std::ios::binary);
        out << R"({"version":1,"triggers":[
            {"spell_id":1,"timing":"FixateLine","showUncertainty":true},
            {"timing":"OnDebuffApplied"},
            {"category":"Actor","timing":"ActorSpawn","anchor":"Nowhere"}
        ]})";
    }

    CurrentEncounter::instance().set(encounter);
    AutoAnnotationTriggerManager manager;
    manager.loadFromFile();
    CurrentEncounter::instance().clear();
    std::filesystem::remove(file);
    std::error_code ec;
    std::filesystem::remove("triggers", ec);

    // The spell trigger without a spell id is dropped; actor triggers need none
    REQUIRE(manager.getTriggers().size() == 2);
    CHECK(manager.getTriggers()[0].timing == TriggerTiming::Fixate);
    CHECK(manager.getTriggers()[0].visualStyle == AnnotationVisualStyle::Hatched);
    CHECK(manager.getTriggers()[1].category == TriggerCategory::Actor);
    CHECK(manager.getTriggers()[1].anchor == TriggerAnchor::Target);
}

TEST_CASE("The wizard hands over rotation in radians and stack colours sorted",
          "[annotation][trigger][wizard]") {
    AutoAnnotationWizardState state;
    state.selectedSpellId_ = 1234;
    state.spellCategory_ = WizardSpellCategory::Debuff;
    state.timing_ = TriggerTiming::OnDebuffApplied;
    state.shape_ = AnnotationShape::Cone;
    state.dimensions_ = {30.0f, 90.0f, 0.5f, 90.0f};  // rotation edited as 90 degrees

    // Entered lowest first, and one more than the four slots via Ctrl+click
    state.stackColorCount_ = 5;
    state.stackThresholds_ = {1, 2, 3, 4};

    const AutoAnnotationTrigger trigger = state.buildTrigger();

    CHECK(trigger.dimensions[3] == Catch::Approx(1.5707963f));
    REQUIRE(trigger.stackColorCount == 4);
    CHECK(trigger.stackColors[0].stackCount == 4);
    CHECK(trigger.stackColors[3].stackCount == 1);
    CHECK(trigger.getColorForStacks(3) == state.stackColors_[2]);
}

TEST_CASE("A wrongly typed annotation field skips that annotation",
          "[annotation][serializer]") {
    std::vector<Annotation> annotations;
    uint64_t maxId = 0;
    REQUIRE(AnnotationSerializer::fromJson(R"({"version":1,"annotations":[
        {"id":1,"shape":"circle","worldX":10.0,"worldY":20.0,"anchorMode":"world"},
        {"id":2,"shape":5},
        {"id":3,"color":["red"]},
        {"id":"4"},
        {"id":5,"visible":"yes"},
        7
    ]})", annotations, maxId));

    REQUIRE(annotations.size() == 1);
    CHECK(annotations[0].id == 1);

    // A bad version is a rejected file, not an assert
    CHECK_FALSE(AnnotationSerializer::fromJson(R"({"version":"1","annotations":[]})",
                                               annotations, maxId));

    const auto shared = AnnotationSerializer::fromShareableJson(R"({"version":1,"annotations":[
        {"x":1.0,"y":2.0},
        {"x":"1","y":2.0}
    ]})");
    CHECK(shared.size() == 1);
}

TEST_CASE("A saved trigger reopens under the list it was made from", "[annotation][trigger]") {
    using Category = UIAutoAnnotationPanel::SelectionCategory;
    auto categoryFor = [](TriggerTiming timing) {
        return UIAutoAnnotationPanel::selectionCategoryFor(TriggerCategory::Spell, timing);
    };
    CHECK(categoryFor(TriggerTiming::Fixate) == Category::Debuff);
    CHECK(categoryFor(TriggerTiming::Tether) == Category::Debuff);
    CHECK(categoryFor(TriggerTiming::OnDamageTaken) == Category::DamageTaken);
    CHECK(categoryFor(TriggerTiming::OnDamage) == Category::Damage);
    CHECK(categoryFor(TriggerTiming::OnCastStart) == Category::Cast);
    CHECK(UIAutoAnnotationPanel::selectionCategoryFor(TriggerCategory::Actor, TriggerTiming::ActorTrail)
          == Category::Actor);
}

TEST_CASE("A trigger's list name covers every shape and anchor", "[annotation][trigger]") {
    AutoAnnotationTrigger trigger;
    trigger.shape = AnnotationShape::Donut;
    trigger.anchor = TriggerAnchor::SourceToTarget;
    CHECK(trigger.getDisplayName("Soak") == "Soak -> Donut @ Caster to Target");
}

TEST_CASE("Fade Over Time fades a trail mark out after its actor dies", "[annotation][trigger]") {
    CachedAnnotationEvent event{};
    event.isActorTrigger = true;
    event.deathBehavior = ActorDeathBehavior::FadeOverTime;
    event.timestamp_ms = 1000;
    event.eventTimestamp_ms = 1000;
    event.actorDeathTime_ms = 5000;

    // An encounter-long mark ends a fade's length after the death
    auto fade = trigger_eval::deathFadeFor(event, INT32_MAX);
    REQUIRE(fade.fades());
    CHECK(fade.start == 5000);
    CHECK(fade.end == 5000 + trigger_eval::kActorDeathFadeMs);
    CHECK(trigger_eval::deathFadeAlpha(fade, 4000) == 1.0f);
    CHECK(trigger_eval::deathFadeAlpha(fade, 5000 + trigger_eval::kActorDeathFadeMs / 2)
          == Catch::Approx(0.5f));

    // A mark that expires before the death is left alone
    CHECK_FALSE(trigger_eval::deathFadeFor(event, 4000).fades());

    // Persist at Death Spot does not fade
    event.deathBehavior = ActorDeathBehavior::PersistAtDeathSpot;
    CHECK_FALSE(trigger_eval::deathFadeFor(event, INT32_MAX).fades());
}

TEST_CASE("Fade Over Time holds the last follow sample through the fade", "[annotation][trigger]") {
    CachedAnnotationEvent last{};
    last.isActorTrigger = true;
    last.deathBehavior = ActorDeathBehavior::FadeOverTime;
    last.isDuringDebuffSample = true;
    last.actorDeathTime_ms = 5000;
    last.timestamp_ms = 4750;
    last.eventTimestamp_ms = 4750;
    last.sampleDurationMs = 250;

    auto fade = trigger_eval::deathFadeFor(last, 5000);
    REQUIRE(fade.fades());
    CHECK(fade.start == 5000);
    CHECK(fade.end == 5000 + trigger_eval::kActorDeathFadeMs);

    CachedAnnotationEvent earlier = last;
    earlier.timestamp_ms = 4000;
    earlier.eventTimestamp_ms = 4000;
    CHECK_FALSE(trigger_eval::deathFadeFor(earlier, 4250).fades());
}
