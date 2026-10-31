#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <set>
#include <cctype>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <rapidjson/document.h>

#include "WarcraftLogsSession.h"
#include "Settings/WclSettings.h"
#include "UnifiedSettings.h"

namespace fs = std::filesystem;

// What arguments characterRankings actually accepts.
//
// The response carries class and spec and no hero tree, which was read as "the
// API cannot filter by hero tree". That does not follow - what comes back and
// what can be asked for are different things, and the WarcraftLogs site itself
// offers a hero talent filter. Asking the schema settles it rather than
// inferring from the reply.
TEST_CASE("characterRankings arguments", "[wcl][live][schema]") {
    if (!std::getenv("AWL_LIVE_WCL")) {
        WARN("AWL_LIVE_WCL not set; skipping");
        return;
    }

    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(false, "./wcl_cache");
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    // Send an argument the current query does not use and read what the server
    // says. GraphQL rejects an unknown argument by name and lists the ones it
    // will take, so one refused query maps the whole surface - cheaper and more
    // reliable than guessing from the response shape.
    // characterRankings takes a "filter" string - WarcraftLogs' own expression
    // language, the one behind the site's advanced filter box. If a hero tree
    // can be expressed there, the sample can be narrowed server side instead of
    // fetching fights only to discard them.
    // characterRankings takes includeCombatantInfo. If the entry then carries
    // talents, the hero tree is knowable at ranking time and the fetch-and-
    // discard goes away - two thirds of an Aldrachi-only pull is currently
    // spent on fights that turn out to be the other tree.
    const std::string query =
        "query { worldData { encounter(id: 3379) { characterRankings("
        "className: \"DemonHunter\", specName: \"Havoc\", difficulty: 5, "
        "metric: dps, page: 1, includeCombatantInfo: true) } } }";

    // Kept apart from the cached API responses beside it: wcl_cache is a corpus
    // the tests read from, and a probe dump is neither a cached reply nor
    // something to be mistaken for one.
    std::error_code ec;
    fs::create_directories("./wcl_cache/_schema_probe", ec);

    const std::string out = "./wcl_cache/_schema_probe/rankings_combatantinfo.json";
    const std::string err = session.dumpRawQuery(query, out);
    if (!err.empty()) { WARN("refused: " << err); return; }

    std::ifstream in(out, std::ios::binary);
    const std::string body((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    UNSCOPED_INFO("bytes: " << body.size());
    UNSCOPED_INFO(body.substr(0, 3000));

    SUCCEED();
}

// Dump the whole v2 schema, so what the API accepts can be read on disk
// rather than found by probing (for example, characterRankings takes
// includeCombatantInfo but has no heroTree argument).
//
// Descriptions included: they are the documentation, and the published docs do
// not cover every argument.
TEST_CASE("dump the warcraftlogs schema", "[wcl][live][schemadump]") {
    if (!std::getenv("AWL_LIVE_WCL")) { WARN("AWL_LIVE_WCL not set; skipping"); return; }

    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(false, "./wcl_cache");
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    // __schema is blocked - it answers "Internal server error" even for just a
    // list of type names - while __type works per type. So the walk starts at
    // the known roots and follows every type a field or argument mentions,
    // which reaches everything reachable from a query.
    // Probe output lives apart from the cached API responses: wcl_cache is a
    // corpus the tests read, and dumps are neither cached replies nor something
    // to confuse with them. The whole build tree is gitignored either way.
    const std::string dir = std::string(WCL_CACHE_DIR) + "/_schema_probe/schema";
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::vector<std::string> queue = { "Query", "WorldData", "ReportData",
                                       "CharacterData", "GuildData", "RateLimitData" };
    std::set<std::string> seen(queue.begin(), queue.end());
    size_t written = 0;

    auto isScalar = [](const std::string& n) {
        return n == "String" || n == "Int" || n == "Float" || n == "Boolean"
            || n == "ID" || n == "JSON" || n == "Json";
    };

    while (!queue.empty()) {
        const std::string name = queue.back();
        queue.pop_back();

        const std::string query =
            "query { __type(name: \"" + name + "\") { kind name description "
            "fields(includeDeprecated: false) { name description "
            "args { name description defaultValue "
            "type { kind name ofType { kind name ofType { kind name } } } } "
            "type { kind name ofType { kind name ofType { kind name } } } } "
            "inputFields { name description type { kind name ofType { kind name } } } "
            "enumValues(includeDeprecated: false) { name description } } }";

        const std::string path = dir + "/" + name + ".json";
        const std::string typeErr = session.dumpRawQuery(query, path);
        if (!typeErr.empty()) {
            UNSCOPED_INFO(name << ": " << typeErr.substr(0, 100));
            continue;
        }
        ++written;

        // Follow every type this one mentions, so the walk covers whatever the
        // roots reach rather than a list guessed up front. Walk the parsed
        // document: the reply is pretty printed, so matching on a raw
        // "name":" substring finds nothing.
        std::ifstream in(path, std::ios::binary);
        const std::string body((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        rapidjson::Document doc;
        doc.Parse(body.c_str());
        if (doc.HasParseError() || !doc.HasMember("data")) continue;

        std::function<void(const rapidjson::Value&)> visit =
            [&](const rapidjson::Value& node) {
                if (node.IsArray()) {
                    for (const auto& child : node.GetArray()) visit(child);
                    return;
                }
                if (!node.IsObject()) return;
                for (auto it = node.MemberBegin(); it != node.MemberEnd(); ++it) {
                    const std::string key = it->name.GetString();
                    // Only names under a type reference are types; a field's
                    // own "name" is a field, and following those invents types.
                    if ((key == "type" || key == "ofType") && it->value.IsObject()) {
                        const auto& ref = it->value;
                        if (ref.HasMember("name") && ref["name"].IsString()) {
                            const std::string found = ref["name"].GetString();
                            if (!found.empty()
                                && std::isupper(static_cast<unsigned char>(found[0]))
                                && found.rfind("__", 0) != 0 && !isScalar(found)
                                && seen.insert(found).second) {
                                queue.push_back(found);
                            }
                        }
                    }
                    visit(it->value);
                }
            };
        visit(doc["data"]["__type"]);
    }

    UNSCOPED_INFO("wrote " << written << " types into " << dir);
    CHECK(written > 20);
}
