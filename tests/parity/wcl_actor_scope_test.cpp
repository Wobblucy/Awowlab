#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

#include "WarcraftLogsSession.h"
#include "Settings/WclSettings.h"
#include "UnifiedSettings.h"
#include "StringInterner.h"
#include "Settings/RateLimitBackup.h"

// Spends real API queries, so it only runs when AWL_LIVE_WCL is set.
//
// What it proves: passing sourceID narrows a fight fetch to one player. A raid
// fight is around forty pages of ten thousand events and every page is a query,
// so a baseline that studies one player per fight would otherwise spend forty
// queries to keep one twentieth of what it downloads.
TEST_CASE("a fight can be fetched for one actor", "[wcl][live]") {
    if (!std::getenv("AWL_LIVE_WCL")) {
        WARN("AWL_LIVE_WCL not set; skipping the live query");
        return;
    }

    // A real ranked Havoc pull, taken from the cached rankings
    const std::string reportCode = "PtdbM8ay4NKF1WvB";
    const int32_t fightId = 26;
    const std::string playerName = "Lazelele";

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(false, "");   // measure the fetch, not the cache

    // Same order the app uses: the hooks are what decrypt the DPAPI-protected
    // token, and initialize() is what reads the file at all. Without both the
    // cache is empty and the token looks absent rather than encrypted.
    awow::registerRateLimitBackups();
    SettingsCache::instance().initialize();

    const auto& settings = SettingsCache::instance().get();
    const std::string token = WclSettings::readFrom(settings).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }
    session.setUserAccessToken(token);

    REQUIRE(session.initialize(reportCode));
    REQUIRE(session.fetchReport());

    const auto* metadata = session.getReportMetadata();
    REQUIRE(metadata != nullptr);

    int32_t actorId = 0;
    for (const auto& actor : metadata->actors) {
        if (actor.name == playerName) { actorId = actor.id; break; }
    }
    INFO("actor id for " << playerName);
    REQUIRE(actorId != 0);

    const auto scoped = session.loadFight(fightId, nullptr, actorId);

    UNSCOPED_INFO("actor id: " << actorId);
    UNSCOPED_INFO("combat events: " << scoped.combatEvents.size());
    UNSCOPED_INFO("cast events:   " << scoped.spellCastEvents.size());
    UNSCOPED_INFO("aura events:   " << scoped.auraEvents.size());
    UNSCOPED_INFO("actors seen:   " << scoped.guidToNameMap.size());

    // The query has to be accepted and return something
    CHECK_FALSE(scoped.combatEvents.empty());
    CHECK_FALSE(scoped.spellCastEvents.empty());

    // guidToNameMap comes from report metadata, so it names every actor in the
    // report whether or not their events came back - it is not a measure of
    // what was fetched. What matters is whose events arrived.
    size_t castsByPlayer = 0;
    for (const auto& cast : scoped.spellCastEvents) {
        auto it = scoped.guidToNameMap.find(cast.actor_guid);
        if (it != scoped.guidToNameMap.end() && it->second == playerName) {
            ++castsByPlayer;
        }
    }
    UNSCOPED_INFO("casts by " << playerName << ": " << castsByPlayer
                  << " of " << scoped.spellCastEvents.size());

    // Nearly all of them. A few can belong to the player's pets, which the
    // server treats as their own sources.
    CHECK(castsByPlayer * 10 >= scoped.spellCastEvents.size() * 9);

    // A page holds ten thousand events and a raid fight runs to about forty
    // pages unfiltered. Landing inside one or two is the whole saving.
    CHECK(scoped.combatEvents.size() < 20000);

    // Does a source-scoped fetch carry damage the player TOOK? The filter is
    // documented as being on the event source, which would exclude it, but
    // that is worth checking against the wire rather than assuming.
    std::string playerGuid;
    for (const auto& [guid, name] : scoped.guidToNameMap) {
        if (name == playerName) { playerGuid = guid; break; }
    }
    REQUIRE_FALSE(playerGuid.empty());

    size_t dealt = 0, taken = 0;
    for (const auto& e : scoped.combatEvents) {
        const auto src = guidInterner().lookup(e.source_guid_id);
        const auto tgt = guidInterner().lookup(e.target_guid_id);
        if (src == playerGuid) ++dealt;
        if (tgt == playerGuid) ++taken;
    }
    UNSCOPED_INFO("combat events dealt BY " << playerName << ": " << dealt);
    UNSCOPED_INFO("combat events done TO " << playerName << ": " << taken);
    UNSCOPED_INFO("defense rows (missed+absorb): "
                  << scoped.missedEvents.size() + scoped.absorbEvents.size());

    // Both directions come back. sourceID reads as "events involving this
    // actor", not "events this actor sourced" - checked because the name
    // suggests otherwise and a package built on the wrong reading would be
    // missing every incoming hit.
    CHECK(dealt > 0);
    CHECK(taken > 0);
}
