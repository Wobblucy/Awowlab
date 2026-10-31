#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <rapidjson/document.h>

#include "WarcraftLogsSession.h"
#include "Auth/Credentials.h"
#include "Auth/UserOAuthClient.h"
#include "Settings/WclSettings.h"
#include "UnifiedSettings.h"

// What each kind of request costs the app's own WarcraftLogs points.
//
// Reads rateLimitData before and after each request. Report requests carry
// @onlySpendUserPoints and are charged to the signed-in user, so they show
// about 0 here; that is the check that they never reach the app's points.
// What they cost the user is not reported anywhere in rateLimitData totals;
// measure it with [rawquery] and currentRequest, one events page at a time. The figure is per account and every client on it shares it,
// so another user spending points at the same moment shows up here too; run
// it twice and trust the smaller numbers.
//
// The cache is off for the session, so every request goes to the server. Set
// AWL_LIVE_WCL to run it; it spends real points.
namespace {

namespace fs = std::filesystem;

struct Points {
    double spent = -1.0;
    double limit = -1.0;
    int resetIn = -1;
};

Points readPoints(warcraftlogs::WarcraftLogsSession& session, const fs::path& dir) {
    const fs::path out = dir / "rate_limit.json";
    const std::string err = session.dumpRawQuery(
        "query { rateLimitData { limitPerHour pointsSpentThisHour pointsResetIn } }",
        out.string());
    INFO(err);
    REQUIRE(err.empty());

    std::ifstream in(out);
    std::stringstream text;
    text << in.rdbuf();
    rapidjson::Document doc;
    doc.Parse(text.str().c_str());
    REQUIRE_FALSE(doc.HasParseError());

    const rapidjson::Value* data = &doc;
    if (data->HasMember("data")) data = &(*data)["data"];
    REQUIRE(data->HasMember("rateLimitData"));
    const auto& rate = (*data)["rateLimitData"];

    Points points;
    points.spent = rate["pointsSpentThisHour"].GetDouble();
    points.limit = rate["limitPerHour"].GetDouble();
    points.resetIn = rate["pointsResetIn"].GetInt();
    return points;
}

std::string costLine(const char* what, const Points& before, const Points& after,
                     double probeCost) {
    std::ostringstream line;
    line << what << ": " << (after.spent - before.spent - probeCost) << " points ("
         << before.spent << " -> " << after.spent << ")";
    return line.str();
}

} // namespace

// Signs in again when the stored token has expired, without the app. The
// authorization URL is written to <temp>/awowlab_point_cost/authorize_url.txt
// for whichever browser approves it; the redirect comes back to the first
// registered port, and the new token is saved the way the sign-in dialog
// saves it. Run with AWL_WCL_SIGNIN set.
TEST_CASE("sign in to WarcraftLogs from a URL", "[wcl][live][signin]") {
    if (!std::getenv("AWL_WCL_SIGNIN")) {
        WARN("AWL_WCL_SIGNIN not set; skipping");
        return;
    }

    SettingsCache::instance().initialize();

    const int port = warcraftlogs::credentials::REDIRECT_PORTS[0];
    warcraftlogs::UserOAuthClient client(warcraftlogs::credentials::CLIENT_ID,
                                         warcraftlogs::credentials::getRedirectUri(port));
    const std::string url = client.getAuthorizationUrl();
    REQUIRE_FALSE(url.empty());

    const fs::path dir = fs::temp_directory_path() / "awowlab_point_cost";
    fs::create_directories(dir);
    std::ofstream(dir / "authorize_url.txt") << url << "\n";

    std::string code;
    std::string error;
    warcraftlogs::http::LocalHttpServer server;
    const bool received = server.waitForCallback(
        [&](const warcraftlogs::http::OAuthCallback& callback) {
            code = callback.code;
            error = callback.error;
        },
        port, 600);
    INFO(error << " " << server.getLastError());
    REQUIRE(received);

    const auto result = client.exchangeCode(code);
    INFO(result.errorMessage);
    REQUIRE(result.success);

    auto& cache = SettingsCache::instance();
    auto wcl = WclSettings::readFrom(cache.get());
    wcl.accessToken = client.getToken();
    if (const auto expiry = client.getTokenExpiry()) {
        wcl.tokenExpiresAt = static_cast<int64_t>(std::chrono::system_clock::to_time_t(*expiry));
    }
    wcl.writeTo(cache.get());
    cache.markDirty();
    cache.forceSave();
}

// Runs the query in AWL_WCL_RAW_QUERY and writes the reply to
// <temp>/awowlab_point_cost/raw_query.json, for asking the API about itself
// (its rate limits, a type's fields) without a named query for it.
TEST_CASE("run one raw query", "[wcl][live][rawquery]") {
    const char* query = std::getenv("AWL_WCL_RAW_QUERY");
    if (!query) {
        WARN("AWL_WCL_RAW_QUERY not set; skipping");
        return;
    }

    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    REQUIRE_FALSE(token.empty());

    const fs::path dir = fs::temp_directory_path() / "awowlab_point_cost";
    fs::create_directories(dir);

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(false);
    session.setUserAccessToken(token);
    const std::string err = session.dumpRawQuery(query, (dir / "raw_query.json").string());
    INFO(err);
    REQUIRE(err.empty());
}

TEST_CASE("point cost of each request kind", "[wcl][live][points]") {
    if (!std::getenv("AWL_LIVE_WCL")) {
        WARN("AWL_LIVE_WCL not set; skipping");
        return;
    }

    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    const fs::path dir = fs::temp_directory_path() / "awowlab_point_cost";
    fs::create_directories(dir);

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(false);
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    // The probe itself costs something; two in a row measure it.
    const Points start = readPoints(session, dir);
    const Points again = readPoints(session, dir);
    const double probe = again.spent - start.spent;
    UNSCOPED_INFO("limit per hour: " << start.limit << ", resets in "
                  << start.resetIn << "s, spent so far " << start.spent);
    UNSCOPED_INFO("rateLimitData probe: " << probe << " points");

    // Rankings: one encounter, as a baseline does for every boss in a zone
    Points before = again;
    REQUIRE(session.dumpCharacterRankings(3379, "DemonHunter", "Havoc", 5,
                                          (dir / "rankings_raid.json").string()).empty());
    Points after = readPoints(session, dir);
    UNSCOPED_INFO(costLine("rankings, raid boss (3379 Mythic)", before, after, probe));

    before = after;
    REQUIRE(session.dumpCharacterRankings(12993, "DemonHunter", "Havoc", 10,
                                          (dir / "rankings_dungeon.json").string()).empty());
    after = readPoints(session, dir);
    UNSCOPED_INFO(costLine("rankings, dungeon (12993 M+)", before, after, probe));

    // A report load: metadata, then one player's events for one fight, the
    // way the app pulls a fight
    for (const char* code : {"2C8qKhQczjmTM146", "w9qF7tvNVZLKbcQ2"}) {
        before = after;
        REQUIRE(session.initialize(code));
        REQUIRE(session.fetchReport());
        after = readPoints(session, dir);
        UNSCOPED_INFO(costLine((std::string("report metadata ") + code).c_str(),
                               before, after, probe));

        const auto* metadata = session.getReportMetadata();
        REQUIRE(metadata != nullptr);
        int32_t fightId = 0;
        int64_t fightMs = 0;
        for (const auto& fight : metadata->fights) {
            if (fight.encounterID > 0) {
                fightId = fight.id;
                fightMs = fight.duration();
                break;
            }
        }
        int32_t actorId = 0;
        for (const auto& actor : metadata->actors) {
            if (actor.type == "Player") { actorId = actor.id; break; }
        }
        REQUIRE(fightId != 0);
        REQUIRE(actorId != 0);

        before = after;
        ExtractedData data = session.loadFight(fightId, nullptr, actorId);
        after = readPoints(session, dir);
        UNSCOPED_INFO(costLine((std::string("fight ") + std::to_string(fightId) + " ("
                                + std::to_string(fightMs / 1000) + "s), one player").c_str(),
                               before, after, probe)
                      << ", " << data.spellCastEvents.size() << " casts");

        // The replay loads the whole fight, every player
        before = after;
        data = session.loadFight(fightId, nullptr);
        after = readPoints(session, dir);
        UNSCOPED_INFO(costLine((std::string("fight ") + std::to_string(fightId)
                                + ", whole fight").c_str(), before, after, probe)
                      << ", " << data.spellCastEvents.size() << " casts");
    }

    UNSCOPED_INFO("total spent by this test: " << (after.spent - start.spent));
    WARN("point costs logged above");
}
