#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "WarcraftLogsSession.h"
#include "Settings/WclSettings.h"
#include "UnifiedSettings.h"
#include "SpellNameDatabase.h"

// Whether a dummy pull is one rotation or several.
//
// A 300s window is a timer, not a kill, so nothing in the fight itself says
// where one attempt ends and the next begins. If the Dome resets cooldowns -
// the way a target dummy room usually does - a long cooldown will be recast
// far sooner than its own cooldown allows, and that recast is the seam between
// attempts. Anything averaged across the whole window would then be describing
// several openers as though they were one rotation.
//
// Reads the ranked Rogue pull, the only public dummy log there is.
TEST_CASE("dummy pulls and cooldown resets", "[wcl][live][dummy][reset]") {
    if (!std::getenv("AWL_LIVE_WCL")) {
        WARN("AWL_LIVE_WCL not set; skipping");
        return;
    }

    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    auto& spellDb = SpellNameDatabase::instance();
    spellDb.setDataDirectory(SPELL_NAMES_DIR);

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(true, "./wcl_cache");
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    // The Diabolical Duo ranking, all three pulls from one report.
    const std::string reportCode = "2C8qKhQczjmTM146";
    REQUIRE(session.initialize(reportCode));
    REQUIRE(session.fetchReport());

    const auto* metadata = session.getReportMetadata();
    REQUIRE(metadata != nullptr);

    UNSCOPED_INFO("report has " << metadata->fights.size() << " fights");
    for (const auto& fight : metadata->fights) {
        UNSCOPED_INFO("  fight " << fight.id << " enc=" << fight.encounterID
                      << " \"" << fight.name << "\" "
                      << (fight.duration() / 1000) << "s kill=" << fight.kill);
    }

    // The ranked player, so the fetch is scoped the way a baseline scopes it.
    int32_t actorId = 0;
    std::string actorName;
    for (const auto& actor : metadata->actors) {
        if (actor.type == "Player") {
            actorId = actor.id;
            actorName = actor.name;
            break;
        }
    }
    REQUIRE(actorId != 0);
    UNSCOPED_INFO("player: " << actorName << " (id " << actorId << ")");

    ExtractedData data;
    try {
        data = session.loadFight(1, nullptr, actorId);
    } catch (const std::exception& e) {
        WARN(std::string("fetch failed: ") + e.what());
        return;
    }
    REQUIRE_FALSE(data.spellCastEvents.empty());

    spellDb.loadSpellIds(data.collectAllSpellIds());

    std::string playerGuid;
    for (const auto& [guid, name] : data.guidToNameMap) {
        if (name == actorName) { playerGuid = guid; break; }
    }

    // Every cast of each spell, in order, so the gaps between them can be read.
    // cooldown_ms comes from the spell database rather than from the log, so a
    // recast can be checked against the cooldown the spell actually has instead
    // of one inferred from the gaps.
    struct Casts {
        std::vector<int32_t> times;
        int32_t cooldownMs = 0;
    };
    std::map<uint32_t, Casts> castsBySpell;
    for (const auto& cast : data.spellCastEvents) {
        if (!playerGuid.empty() && cast.actor_guid != playerGuid) continue;
        auto& entry = castsBySpell[cast.spell_id];
        entry.times.push_back(cast.end_timestamp_ms);
        entry.cooldownMs = std::max(entry.cooldownMs, cast.cooldown_ms);
    }

    UNSCOPED_INFO(castsBySpell.size() << " distinct spells cast over "
                  << data.spellCastEvents.size() << " casts");

    const int32_t firstCast = data.spellCastEvents.front().end_timestamp_ms;
    const int32_t lastCast = data.spellCastEvents.back().end_timestamp_ms;
    UNSCOPED_INFO("casts span " << ((lastCast - firstCast) / 1000) << "s");

    // cooldown_ms is populated from SpellGCDDatabase, which has nothing for
    // these spells - every one came back zero. So the cooldown cannot be looked
    // up and has to be read off the casts themselves: for a spell used on
    // cooldown the typical gap between casts is its cooldown, and a reset shows
    // up as a gap far below that typical one.
    size_t suspected = 0;
    size_t longCooldownSpells = 0;
    for (auto& [spellId, entry] : castsBySpell) {
        auto& times = entry.times;
        std::sort(times.begin(), times.end());

        std::vector<int32_t> gaps;
        for (size_t i = 1; i < times.size(); ++i) {
            gaps.push_back(times[i] - times[i - 1]);
        }
        if (gaps.empty()) continue;

        std::vector<int32_t> sortedGaps = gaps;
        std::sort(sortedGaps.begin(), sortedGaps.end());
        const int32_t median = sortedGaps[sortedGaps.size() / 2];
        const int32_t shortest = sortedGaps.front();

        const auto name = spellDb.hasSpellName(spellId)
                            ? spellDb.getSpellName(spellId)
                            : std::string("spell ") + std::to_string(spellId);
        UNSCOPED_INFO("  [" << spellId << "] " << name << ": " << times.size()
                      << " casts, median gap " << (median / 1000)
                      << "s, shortest " << (shortest / 1000) << "s");

        // Only spells whose usual spacing looks like a real cooldown. A filler
        // recast every few seconds has no cooldown to reset. Needs enough casts
        // for the median to mean anything.
        if (median < 30000 || times.size() < 4) continue;
        ++longCooldownSpells;

        // Half the usual spacing is not something the rotation produces on its
        // own; it means the cooldown came back early.
        if (shortest * 2 < median) {
            ++suspected;
            UNSCOPED_INFO("    ^ recast in half its usual spacing");
        }
    }

    UNSCOPED_INFO(longCooldownSpells << " spells went out on cooldown-like spacing");
    UNSCOPED_INFO(suspected << " of those came back early");

    // The finding is only meaningful if something was examined. The first run
    // of this reported "no resets" having looked at nothing, because every
    // cooldown_ms was zero.
    CHECK(longCooldownSpells > 0);
}
