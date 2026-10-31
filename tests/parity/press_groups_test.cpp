#include <catch2/catch_test_macros.hpp>

#include "Database/AnalysisPackage/PressGroups.h"
#include "Database/EventTypes.h"
#include "Database/SpellCastEvent.h"

#include <string>
#include <vector>

using namespace awow::analysis;

namespace {

SpellCastEvent cast(uint32_t spellId, int32_t at) {
    SpellCastEvent e{};
    e.actor_guid = "player";
    e.spell_id = spellId;
    e.end_timestamp_ms = at;
    return e;
}

} // namespace

// The grouping has to put an ability and everything it spawns into one
// question, and leave unrelated presses apart. Built from a synthetic timeline
// rather than a log so the expected answer is not in doubt.
TEST_CASE("casts that travel together become one question", "[press]") {
    ExtractedData data;
    data.spellNameMap = {{100, "Blade Dance"}, {101, "Thrown Glaive"},
                         {200, "Chaos Strike"}, {300, "Filler Proc"}};

    // Blade Dance always throws a glaive 60ms later; Chaos Strike is alone;
    // the proc fires constantly and far too often to be pressed.
    int32_t t = 0;
    for (int i = 0; i < 20; ++i) {
        data.spellCastEvents.push_back(cast(100, t));
        data.spellCastEvents.push_back(cast(101, t + 60));
        data.spellCastEvents.push_back(cast(200, t + 4000));
        for (int k = 0; k < 4; ++k) data.spellCastEvents.push_back(cast(300, t + 1200 + k * 700));
        t += 9000;
    }

    FightCasts fight;
    fight.data = &data;
    fight.playerGuid = "player";
    fight.durationMs = t;

    const auto groups = groupCasts({fight});
    REQUIRE_FALSE(groups.empty());

    // Blade Dance and its glaive must be one group; nothing else joins them.
    bool found = false;
    for (const auto& group : groups) {
        std::vector<uint32_t> ids;
        for (const auto& o : group.options) ids.push_back(o.spellId);
        if (std::find(ids.begin(), ids.end(), 100u) == ids.end()) continue;
        found = true;
        INFO("group with Blade Dance has " << ids.size() << " options");
        CHECK(std::find(ids.begin(), ids.end(), 101u) != ids.end());
        CHECK(std::find(ids.begin(), ids.end(), 200u) == ids.end());
    }
    CHECK(found);

    // The proc fires about 27 times a minute, far over the ceiling, so the
    // player is told it was excluded rather than asked about it.
    for (const auto& group : groups) {
        for (const auto& o : group.options) {
            if (o.spellId == 300) {
                INFO("proc at " << o.perMinute << "/min");
                CHECK(o.excludedByRate);
            }
            if (o.spellId == 100 || o.spellId == 200) CHECK_FALSE(o.excludedByRate);
        }
    }
}

// A press that always spawns something stands alone almost never, which is why
// the alone fraction is reported and not acted on.
TEST_CASE("a press that always spawns a child is not marked as one", "[press]") {
    ExtractedData data;
    data.spellNameMap = {{100, "Death Sweep"}, {101, "Thrown Glaive"}};
    int32_t t = 0;
    for (int i = 0; i < 30; ++i) {
        data.spellCastEvents.push_back(cast(100, t));
        data.spellCastEvents.push_back(cast(101, t + 40));
        t += 6000;
    }
    FightCasts fight;
    fight.data = &data;
    fight.playerGuid = "player";
    fight.durationMs = t;

    const auto groups = groupCasts({fight});
    REQUIRE(groups.size() == 1);
    for (const auto& o : groups.front().options) {
        INFO(o.name << " alone " << o.aloneFraction);
        CHECK(o.aloneFraction < 0.1);      // indistinguishable on timing alone
        CHECK_FALSE(o.excludedByRate);     // and neither is ruled out by rate
    }
}

// Two abilities used independently will collide occasionally by chance. The
// grouping must not merge them on a handful of coincidences, or the player is
// asked which of two unrelated presses they press.
TEST_CASE("an occasional collision does not merge two abilities", "[press]") {
    ExtractedData data;
    data.spellNameMap = {{100, "Eye Beam"}, {200, "Essence Break"}};

    int32_t t = 0;
    for (int i = 0; i < 40; ++i) {
        data.spellCastEvents.push_back(cast(100, t));
        // Essence Break lands well clear of Eye Beam most of the time, and
        // within the window on a few - below the threshold for a relationship.
        data.spellCastEvents.push_back(cast(200, (i % 9 == 0) ? t + 200 : t + 5000));
        t += 12000;
    }

    FightCasts fight;
    fight.data = &data;
    fight.playerGuid = "player";
    fight.durationMs = t;

    const auto groups = groupCasts({fight});
    INFO("collisions: " << (40 / 9 + 1) << ", threshold " << kMinPairMatches);
    // Each ability keeps its own question.
    CHECK(groups.size() == 2);
    for (const auto& group : groups) CHECK(group.options.size() == 1);
}
