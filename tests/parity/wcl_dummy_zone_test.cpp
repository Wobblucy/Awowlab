#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "WarcraftLogsSession.h"
#include "Models/RankingSample.h"
#include "Models/SeasonZones.h"
#include "Settings/WclSettings.h"
#include "Settings/RateLimitBackup.h"
#include "UnifiedSettings.h"

namespace fs = std::filesystem;

// Dummy Dome is a zone of target-dummy configurations rather than bosses -
// one target, two, three, five, and a healing check. It is the controlled
// setting a rotation is measured in, so it wants its own package rather than
// being averaged in with encounters.
//
// This asks whether anyone has logged there yet. The zone is unfrozen and the
// content is not on live, so an empty answer is the expected one for now and
// is reported rather than failed - it tells us the dummy package has to read
// local logs until the zone fills up.
TEST_CASE("dummy dome rankings", "[wcl][live][dummy]") {
    if (!std::getenv("AWL_LIVE_WCL")) {
        WARN("AWL_LIVE_WCL not set; skipping");
        return;
    }

    awow::registerRateLimitBackups();
    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    struct Config { int32_t id; const char* name; };
    // Zone 52, read out of the cached season listing.
    const std::vector<Config> configs = {
        { 3591, "Sinister Single"  },
        { 3590, "Diabolical Duo"   },
        { 3592, "Terrible Trio"    },
        { 3593, "Fearsome Five"    },
        { 3594, "Hazardous Healer" },
    };

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(true, "./wcl_cache");
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    const std::string outDir = "./wcl_cache/rankings_dummy";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    size_t withPulls = 0;
    for (const auto& config : configs) {
        const std::string path =
            outDir + "/" + std::to_string(config.id) + ".json";

        // Difficulty 3 - Normal is all this zone offers.
        const std::string error = session.dumpCharacterRankings(
            config.id, "", "", 3, path);
        if (!error.empty()) {
            UNSCOPED_INFO(config.name << ": " << error);
            continue;
        }

        std::ifstream in(path, std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

        warcraftlogs::RankingSample sample;
        warcraftlogs::parseRankingReply(json, config.id, sample);
        if (!sample.pulls.empty()) ++withPulls;

        UNSCOPED_INFO(config.name << " (" << config.id << "): "
                      << sample.pulls.size() << " pulls, "
                      << json.size() << " bytes");
    }

    UNSCOPED_INFO(withPulls << " of " << configs.size()
                  << " dummy configurations have public logs");
    // Nothing to assert about how much of the world has logged unreleased
    // content; the run is here for what it prints.
    SUCCEED();
}

// The dummy zone has to reach the sampler as its own kind, with each
// configuration carrying its own encounter id. That is what lets the ranking
// loop query per configuration rather than asking for the zone - there is no
// zone-wide rankings query, and pooling the configurations would be wrong
// anyway: a one target test says nothing about a five target one.
TEST_CASE("dummy configurations are separate encounters", "[wcl][season][dummy]") {
    const std::string path = std::string(WCL_CACHE_DIR) + "/season_zones.json";
    if (!fs::exists(path)) { WARN("no season listing cached; skipping"); return; }

    std::ifstream in(path, std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    REQUIRE_FALSE(json.empty());

    const auto dummies =
        warcraftlogs::parseSeasonZones(json, warcraftlogs::ZoneKind::Dummy);
    REQUIRE(dummies.size() == 1);

    const auto& dome = dummies.front();
    UNSCOPED_INFO("dummy zone: " << dome.name << " (" << dome.id << ")");

    // Five configurations, each its own encounter id. The sampler keys its
    // cache and its per-encounter pull cap on that id, so duplicates would
    // silently merge two configurations into one sample.
    CHECK(dome.encounters.size() == 5);

    std::vector<int32_t> ids;
    for (const auto& enc : dome.encounters) {
        INFO("configuration " << enc.name << " (" << enc.id << ")");
        CHECK(enc.id > 0);
        CHECK_FALSE(enc.name.empty());
        ids.push_back(enc.id);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    // Normal and nothing harder. This is what separates a dummy zone from a
    // single boss raid, which still lists all four tiers.
    const auto& diffs = dome.difficultyIds;
    CHECK(std::find(diffs.begin(), diffs.end(), 3) != diffs.end());
    CHECK(std::find(diffs.begin(), diffs.end(), 4) == diffs.end());
    CHECK(std::find(diffs.begin(), diffs.end(), 5) == diffs.end());

    // And the dummy zone must not appear on the raid or keystone side, or a
    // raid baseline would quietly include dummy pulls.
    for (const auto kind : { warcraftlogs::ZoneKind::Raid,
                             warcraftlogs::ZoneKind::Dungeon }) {
        for (const auto& zone : warcraftlogs::parseSeasonZones(json, kind)) {
            CHECK(zone.id != dome.id);
        }
    }
}

// Refetch the raid rankings for one spec, the way the app's Pull rankings card
// does. Exists because the cached replies predate includeCombatantInfo and so
// carry no talents - without them a tree-specific baseline has nothing to
// select on. One query per encounter.
TEST_CASE("refresh havoc rankings", "[wcl][live][refresh]") {
    if (!std::getenv("AWL_LIVE_WCL")) { WARN("AWL_LIVE_WCL not set; skipping"); return; }

    awow::registerRateLimitBackups();
    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    const std::string zonesPath = std::string(WCL_CACHE_DIR) + "/season_zones.json";
    if (!fs::exists(zonesPath)) { WARN("no season listing cached; skipping"); return; }
    std::ifstream zin(zonesPath, std::ios::binary);
    const std::string zonesJson((std::istreambuf_iterator<char>(zin)),
                                std::istreambuf_iterator<char>());
    REQUIRE_FALSE(zonesJson.empty());

    const auto raids = warcraftlogs::parseSeasonZones(
        zonesJson, warcraftlogs::ZoneKind::Raid);
    REQUIRE_FALSE(raids.empty());

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(true, "./wcl_cache");
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    const std::string dir = "./wcl_cache/rankings";
    std::error_code ec;
    fs::create_directories(dir, ec);

    for (const auto& enc : raids.front().encounters) {
        const std::string path = dir + "/" + std::to_string(enc.id)
                               + "_DemonHunter_Havoc.json";
        const std::string err = session.dumpCharacterRankings(
            enc.id, "DemonHunter", "Havoc", 5, path);
        UNSCOPED_INFO(enc.name << (err.empty() ? ": ok" : ": " + err));
    }
    SUCCEED();
}
