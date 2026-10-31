#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <map>
#include <cstdlib>
#include <set>
#include "Models/Fight.h"
#include "Encounter/EncounterSegment.h"
#include <functional>
#include <cstring>
#include "Parser/SpecializationColors.h"
#include <rapidjson/document.h>

namespace fs = std::filesystem;

// Validates the two rules the WCL adapter now applies, against the cached
// report pages in the debug build's wcl_cache. Those files are real API
// responses, so this checks the arithmetic against the shape the API actually
// sends rather than against a fixture someone wrote by hand - and it costs no
// API calls.
//
// WCL_CACHE_DIR is empty on a machine that has never pulled a report, in which
// case every case here warns and skips.
namespace {

std::vector<fs::path> cachedEventPages(size_t limit) {
    std::vector<fs::path> pages;
    const std::string root = WCL_CACHE_DIR;
    if (root.empty() || !fs::exists(root)) return pages;

    std::error_code ec;
    for (const auto& report : fs::directory_iterator(root, ec)) {
        if (!report.is_directory()) continue;
        for (const auto& fight : fs::directory_iterator(report.path(), ec)) {
            if (!fight.is_directory()) continue;
            const auto page = fight.path() / "page1.json";
            if (fs::exists(page)) pages.push_back(page);
            if (pages.size() >= limit) return pages;
        }
    }
    return pages;
}

bool loadJson(const fs::path& path, rapidjson::Document& doc) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    doc.Parse(text.c_str());
    return !doc.HasParseError() && doc.IsArray();
}

int intOr(const rapidjson::Value& v, const char* key, int fallback = 0) {
    if (!v.HasMember(key)) return fallback;
    const auto& f = v[key];
    return f.IsInt() ? f.GetInt() : fallback;
}

} // namespace

// The adapter reports amount as resourceChange minus waste, because WCL's
// resourceChange includes the part that spilled over while a local log's amount
// does not. If waste could exceed the change, that subtraction would produce a
// negative gain and the overcap numbers would be nonsense.
TEST_CASE("waste never exceeds the reported resource change", "[wcl][resource]") {
    const auto pages = cachedEventPages(40);
    if (pages.empty()) { WARN("no cached WCL pages; skipping"); return; }

    int seen = 0, fullyWasted = 0, partlyWasted = 0, drains = 0;
    for (const auto& page : pages) {
        rapidjson::Document doc;
        if (!loadJson(page, doc)) continue;

        for (const auto& ev : doc.GetArray()) {
            if (!ev.HasMember("type") || !ev["type"].IsString()) continue;
            if (std::string(ev["type"].GetString()) != "resourcechange") continue;

            const int change = intOr(ev, "resourceChange");
            const int waste = intOr(ev, "waste");
            ++seen;

            CHECK(waste >= 0);
            if (change < 0) {
                // A drain, which the game also reports as a resourcechange.
                // Spending cannot overflow, so waste is always zero here - if
                // that ever changed, amount - waste would inflate the spend.
                ++drains;
                CHECK(waste == 0);
            } else {
                // For a gain, resourceChange includes whatever spilled over, so
                // waste can never be the larger of the two
                CHECK(waste <= change);
                if (waste == change && change > 0) ++fullyWasted;
                else if (waste > 0) ++partlyWasted;
            }
        }
    }

    UNSCOPED_INFO("resourcechange events: " << seen
                  << "  fully wasted: " << fullyWasted
                  << "  partly wasted: " << partlyWasted
                  << "  drains: " << drains);
    CHECK(seen > 0);
    // Both shapes have to appear, or the subtraction is untested in practice
    CHECK(fullyWasted > 0);
    CHECK(partlyWasted > 0);
    // Drains appear too, and they are why the gain path needs its own branch
    CHECK(drains > 0);
}

// Item level is averaged over worn, non-cosmetic slots. Reports include every
// slot: an unfilled one arrives with id 0, and a shirt or tabard as itemLevel 1.
// Averaging the raw array drags a real character well below their actual gear.
TEST_CASE("item level skips empty and cosmetic slots", "[wcl][gear]") {
    const auto pages = cachedEventPages(40);
    if (pages.empty()) { WARN("no cached WCL pages; skipping"); return; }

    int combatants = 0;
    int sawCosmeticOrEmpty = 0;

    for (const auto& page : pages) {
        rapidjson::Document doc;
        if (!loadJson(page, doc)) continue;

        for (const auto& ev : doc.GetArray()) {
            if (!ev.HasMember("type") || !ev["type"].IsString()) continue;
            if (std::string(ev["type"].GetString()) != "combatantinfo") continue;
            if (!ev.HasMember("gear") || !ev["gear"].IsArray()) continue;

            ++combatants;

            unsigned rawSum = 0, rawCount = 0;
            unsigned realSum = 0, realCount = 0;
            for (const auto& piece : ev["gear"].GetArray()) {
                const int id = intOr(piece, "id");
                const int ilvl = intOr(piece, "itemLevel");
                rawSum += static_cast<unsigned>(ilvl);
                ++rawCount;
                if (id == 0 || ilvl <= 1) { ++sawCosmeticOrEmpty; continue; }
                realSum += static_cast<unsigned>(ilvl);
                ++realCount;
            }

            REQUIRE(realCount > 0);
            const unsigned real = realSum / realCount;

            // A plausible item level, not an average dragged down by tabards
            CHECK(real > 1);
            CHECK(realCount <= rawCount);

            if (realCount < rawCount) {
                const unsigned raw = rawSum / rawCount;
                // Excluded slots only ever lower the raw figure, so the filtered
                // average must come out at least as high
                CHECK(real >= raw);
            }
        }
    }

    UNSCOPED_INFO("combatants: " << combatants
                  << "  empty/cosmetic slots seen: " << sawCosmeticOrEmpty);
    CHECK(combatants > 0);
    // If no report had one, the filter would be untested
    CHECK(sawCosmeticOrEmpty > 0);
}

// The cache is written from our own structs and read back into them, so a field
// the writer forgets is lost on the next load without any error. keystoneLevel
// was parsed from the live response for a while but never read back, and
// difficulty was absent from older cache files for the same reason - this
// guards the pair that matters for the manifest.
TEST_CASE("cached fight metadata round-trips the manifest fields", "[wcl][cache]") {
    const std::string root = WCL_CACHE_DIR;
    if (root.empty() || !fs::exists(root)) { WARN("no wcl cache; skipping"); return; }

    int reports = 0, withDifficulty = 0, withKeystone = 0, withKill = 0;
    std::error_code ec;
    for (const auto& report : fs::directory_iterator(root, ec)) {
        if (!report.is_directory()) continue;
        const auto meta = report.path() / "report_meta.json";
        if (!fs::exists(meta)) continue;

        rapidjson::Document doc;
        {
            std::ifstream in(meta, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            doc.Parse(text.c_str());
        }
        if (doc.HasParseError() || !doc.IsObject()) continue;
        if (!doc.HasMember("fights") || !doc["fights"].IsArray()) continue;

        ++reports;
        for (const auto& f : doc["fights"].GetArray()) {
            if (f.HasMember("difficulty")) ++withDifficulty;
            if (f.HasMember("keystoneLevel")) ++withKeystone;
            if (f.HasMember("kill")) ++withKill;
        }
    }

    UNSCOPED_INFO("reports: " << reports
                  << "  fights carrying difficulty: " << withDifficulty
                  << "  keystoneLevel: " << withKeystone
                  << "  kill: " << withKill);
    CHECK(reports > 0);

    // Files written before a field existed simply lack it, which is why this
    // reports counts rather than asserting every fight has all three. What it
    // does assert is that the writer emits them now - a cache written today and
    // missing difficulty or keystoneLevel would mean the writer regressed.
    CHECK(withDifficulty > 0);
    CHECK(withKeystone > 0);
}

// No cached report predates the kill field, so this feeds the parser the shape
// the API sends rather than waiting for a live pull. WCL reports kill as null on
// a trash fight, which is neither a kill nor a wipe and must not read as false
// by accident - hence a tri-state on the struct rather than a bare bool.
TEST_CASE("kill parses from the fight shape the api sends", "[wcl][cache]") {
    const char* json = R"([
      {"id": 1, "name": "Boss", "kill": true,  "difficulty": 16, "keystoneLevel": 0},
      {"id": 2, "name": "Boss", "kill": false, "difficulty": 16, "keystoneLevel": 0},
      {"id": 3, "name": "Trash", "kill": null, "difficulty": 0,  "keystoneLevel": 0},
      {"id": 4, "name": "Dungeon", "difficulty": 8, "keystoneLevel": 12}
    ])";

    rapidjson::Document doc;
    doc.Parse(json);
    REQUIRE_FALSE(doc.HasParseError());
    REQUIRE(doc.IsArray());

    // Mirrors the reader in WarcraftLogsManager and ResponseCache: only a real
    // bool is taken, so null and absent both leave the default alone
    std::vector<bool> parsed;
    std::vector<int> keystones;
    for (const auto& f : doc.GetArray()) {
        bool kill = false;
        if (f.HasMember("kill") && f["kill"].IsBool()) kill = f["kill"].GetBool();
        parsed.push_back(kill);

        int keystone = 0;
        if (f.HasMember("keystoneLevel") && f["keystoneLevel"].IsInt()) {
            keystone = f["keystoneLevel"].GetInt();
        }
        keystones.push_back(keystone);
    }

    REQUIRE(parsed.size() == 4);
    CHECK(parsed[0] == true);    // killed
    CHECK(parsed[1] == false);   // wiped
    CHECK(parsed[2] == false);   // null - trash, defaults rather than throwing
    CHECK(parsed[3] == false);   // absent - older report shape

    // And the keystone level that was being dropped entirely
    CHECK(keystones[3] == 12);
}

// WCL numbers difficulties 1/3/4/5 (and 10 for keystones) where WoW uses
// 14/15/16. The schemes overlap, so an untranslated WCL value does not read as
// unknown - it reads as a different, real difficulty. Before this translation a
// Mythic report showed as "10 Heroic" and a keystone run as "Unknown".
TEST_CASE("wcl difficulty translates to the wow scheme", "[wcl][difficulty]") {
    using warcraftlogs::wclDifficultyToWow;

    CHECK(wclDifficultyToWow(3) == 14);    // Normal
    CHECK(wclDifficultyToWow(4) == 15);    // Heroic
    CHECK(wclDifficultyToWow(5) == 16);    // Mythic
    CHECK(wclDifficultyToWow(10) == 8);    // Mythic Keystone
    CHECK(wclDifficultyToWow(1) == 17);    // LFR

    // Anything we have not seen returns 0, which getDifficultyName reports as
    // Unknown. Guessing would put a confident wrong label in the manifest.
    CHECK(wclDifficultyToWow(99) == 0);
    CHECK(wclDifficultyToWow(0) == 0);

    // The translated values must be ones getDifficultyName actually knows,
    // otherwise this just moves the wrong label somewhere else
    CHECK(std::string(getDifficultyName(wclDifficultyToWow(5))) == "Mythic");
    CHECK(std::string(getDifficultyName(wclDifficultyToWow(4))) == "Heroic");
    CHECK(std::string(getDifficultyName(wclDifficultyToWow(3))) == "Normal");
    CHECK(std::string(getDifficultyName(wclDifficultyToWow(10))) == "Mythic Keystone");
}

// Every difficulty value the cached reports actually contain has to translate,
// or some real report still gets a wrong label.
TEST_CASE("every cached difficulty value is covered", "[wcl][difficulty]") {
    const std::string root = WCL_CACHE_DIR;
    if (root.empty() || !fs::exists(root)) { WARN("no wcl cache; skipping"); return; }

    std::map<int, int> counts;
    std::error_code ec;
    for (const auto& report : fs::directory_iterator(root, ec)) {
        if (!report.is_directory()) continue;
        const auto meta = report.path() / "report_meta.json";
        if (!fs::exists(meta)) continue;

        rapidjson::Document doc;
        std::ifstream in(meta, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        doc.Parse(text.c_str());
        if (doc.HasParseError() || !doc.IsObject()) continue;
        if (!doc.HasMember("fights") || !doc["fights"].IsArray()) continue;

        for (const auto& f : doc["fights"].GetArray()) {
            if (f.HasMember("difficulty") && f["difficulty"].IsInt()) {
                counts[f["difficulty"].GetInt()]++;
            }
        }
    }

    if (counts.empty()) { WARN("no difficulty values cached; skipping"); return; }

    // The raid and keystone values a baseline actually queries. Reports pulled
    // for other reasons carry difficulties from content this does not target -
    // 108 turned up on a zone nobody asked for - and mapping those on a guess
    // would put a confident wrong label in a package.
    const std::set<int> targeted = {1, 3, 4, 5, 10};

    int unmapped = 0;
    for (const auto& [wcl, n] : counts) {
        const uint32_t wow = warcraftlogs::wclDifficultyToWow(wcl);
        INFO("wcl difficulty " << wcl << " on " << n << " fights -> wow " << wow);

        if (targeted.count(wcl)) {
            CHECK(wow != 0);
            CHECK(std::string(getDifficultyName(wow)) != "Unknown");
        } else if (wow == 0) {
            // Reported rather than asserted: an unknown value reads as Unknown,
            // which is the honest answer, and this keeps it visible.
            ++unmapped;
            WARN("wcl difficulty " << wcl << " (" << n
                 << " fights) has no mapping and will show as Unknown");
        }
    }
    UNSCOPED_INFO("difficulty values with no mapping: " << unmapped);
    UNSCOPED_INFO("distinct difficulty values seen: " << counts.size());
}

#include "Models/SeasonZones.h"

// The zone listing holds every expansion WCL has tracked, and several copies of
// the current season within one: a PTR zone, a beta zone, and last season's
// frozen copy all sit beside the live one. Picking the wrong one would rank
// against content nobody is running.
TEST_CASE("the live season is picked out of the zone listing", "[wcl][season]") {
    const std::string path = std::string(WCL_CACHE_DIR) + "/season_zones.json";
    if (!fs::exists(path)) { WARN("no season listing cached; skipping"); return; }

    std::ifstream in(path, std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    REQUIRE_FALSE(json.empty());

    using warcraftlogs::ZoneKind;
    const auto all = warcraftlogs::parseAllSeasonZones(json);
    const auto dungeons  = warcraftlogs::parseSeasonZones(json, ZoneKind::Dungeon);
    const auto raids     = warcraftlogs::parseSeasonZones(json, ZoneKind::Raid);
    const auto dummies   = warcraftlogs::parseSeasonZones(json, ZoneKind::Dummy);
    const auto aggregates = warcraftlogs::parseSeasonZones(json, ZoneKind::Aggregate);

    REQUIRE_FALSE(dungeons.empty());
    REQUIRE_FALSE(raids.empty());

    UNSCOPED_INFO("dungeon zone: " << dungeons.front().name
                  << " (" << dungeons.front().encounters.size() << ")");
    UNSCOPED_INFO("raid zone:    " << raids.front().name
                  << " (" << raids.front().encounters.size() << ")");

    // A keystone zone offers exactly one difficulty, "Dungeon"; raids offer the
    // four raid tiers. That is what separates them, not the zone name.
    CHECK(dungeons.front().difficultyIds.size() == 1);
    CHECK(dungeons.front().difficultyIds.front() == 10);
    CHECK(raids.front().difficultyIds.size() > 1);

    // Mythic has to be offered, or a baseline cannot be taken at the level
    // people actually push
    const auto& raidDiffs = raids.front().difficultyIds;
    CHECK(std::find(raidDiffs.begin(), raidDiffs.end(), 5) != raidDiffs.end());

    // Every encounter needs a usable id and a name, since both go in a package
    for (const auto& zone : {dungeons.front(), raids.front()}) {
        for (const auto& enc : zone.encounters) {
            INFO("encounter " << enc.id << " in " << zone.name);
            CHECK(enc.id > 0);
            CHECK_FALSE(enc.name.empty());
        }
    }

    // Nothing may be classified by name. Every name rule tried had a real
    // counterexample - the live raid and its PTR copy differ by a parenthesis,
    // and "Dummy Dome" turned out to be real content - so these check that the
    // shape rules put each zone in the right bucket.
    //
    // A dummy zone offers Normal and nothing harder. That is what separates it
    // from Sporefall, a genuine single-boss raid which still lists all four
    // tiers.
    for (const auto& zone : dummies) {
        INFO("dummy zone: " << zone.name);
        const auto& diffs = zone.difficultyIds;
        CHECK(std::find(diffs.begin(), diffs.end(), 4) == diffs.end());
        CHECK(std::find(diffs.begin(), diffs.end(), 5) == diffs.end());
        CHECK(std::find(diffs.begin(), diffs.end(), 3) != diffs.end());
    }

    // An aggregate zone ranks a whole instance as one encounter, so its pulls
    // run hours where a boss runs minutes. WCL numbers them in the 500s while
    // live season zones are two digit, which is what identifies them - shape
    // alone cannot, because a PTR copy of a one-boss raid looks identical.
    for (const auto& zone : aggregates) {
        INFO("aggregate zone: " << zone.name);
        CHECK(zone.id >= 500);
    }

    // No PTR copy may survive, whatever its frozen flag says. A PTR encounter
    // id is the live id plus 50000 - Rotmire is 3159 live and 53159 on the PTR,
    // Altar of Fangs 12993 and 62993 - and Sporefall (PTR) is unfrozen while
    // the live Sporefall it copies is frozen, so the offset is the only thing
    // that excludes it.
    //
    // The offset cannot be read off an id alone: dungeon ids are natively five
    // digits, so a rule on digit count throws away the live keystone zone. What
    // identifies a copy is the original existing beside it.
    std::vector<int32_t> liveIds;
    for (const auto& zone : all) {
        for (const auto& enc : zone.encounters) liveIds.push_back(enc.id);
    }
    for (const auto& zone : all) {
        INFO("zone: " << zone.name << " (" << zone.id << ")");
        for (const auto& enc : zone.encounters) {
            const int32_t original = enc.id - 50000;
            INFO("encounter " << enc.id << " would copy " << original);
            CHECK(std::find(liveIds.begin(), liveIds.end(), original)
                  == liveIds.end());
        }
    }

    // Specifically: the live keystone zone survives with its five digit ids,
    // and Sporefall (PTR) does not survive at all.
    CHECK(dungeons.front().encounters.front().id > 10000);
    for (const auto& zone : all) {
        CHECK(zone.name.find("(PTR)") == std::string::npos);
    }

    // Whatever the kinds are, no zone may land in two of them, and every live
    // zone has to land in one.
    CHECK(all.size() == dungeons.size() + raids.size()
                      + dummies.size() + aggregates.size());

    // And exactly one real raid zone should survive
    CHECK(raids.size() == 1);
}

#include <chrono>

// The season list and the rankings are both cached because they are fetched far
// more often than they change, and a baseline spends one query per encounter -
// ten for a raid. This checks the age rule those caches share.
TEST_CASE("a cached file is reused until it ages out", "[wcl][freshness]") {
    // Mirrors isFresherThan in WarcraftLogsSession.cpp. The rule is what is
    // under test; the file mtime it reads is filesystem behaviour.
    auto fresherThan = [](const fs::path& path, std::chrono::hours maxAge) {
        if (!fs::exists(path)) return false;
        std::error_code ec;
        const auto written = fs::last_write_time(path, ec);
        if (ec) return false;
        return (fs::file_time_type::clock::now() - written) < maxAge;
    };

    const auto dir = fs::temp_directory_path() / "awl_freshness";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const auto file = dir / "cached.json";
    { std::ofstream out(file); out << "{}"; }

    constexpr std::chrono::hours week{24 * 7};

    // Just written, so a rebuild should read it rather than refetch
    CHECK(fresherThan(file, week));

    // Backdated past the window, so the next press pays for a fetch
    fs::last_write_time(file,
        fs::file_time_type::clock::now() - std::chrono::hours(24 * 8), ec);
    CHECK_FALSE(fresherThan(file, week));

    // Inside the window again
    fs::last_write_time(file,
        fs::file_time_type::clock::now() - std::chrono::hours(24 * 6), ec);
    CHECK(fresherThan(file, week));

    // A file that is not there is never fresh - that is what makes the first
    // baseline actually fetch
    CHECK_FALSE(fresherThan(dir / "absent.json", week));

    fs::remove_all(dir, ec);
}

// GraphQL reports a rejected argument inside a 200 response rather than by
// failing the request, so a query can "succeed" while returning nothing. Ten
// rankings pulls wrote ten files that all said "Invalid class and spec
// specified" and reported success.
TEST_CASE("an error inside a 200 response is found", "[wcl][graphql]") {
    // Mirrors findEmbeddedError in WarcraftLogsSession.cpp
    std::function<const char*(const rapidjson::Value&)> findError =
        [&](const rapidjson::Value& v) -> const char* {
            if (v.IsObject()) {
                for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
                    if (std::strcmp(it->name.GetString(), "error") == 0 &&
                        it->value.IsString()) {
                        return it->value.GetString();
                    }
                    if (const char* n = findError(it->value)) return n;
                }
            } else if (v.IsArray()) {
                for (const auto& e : v.GetArray()) {
                    if (const char* n = findError(e)) return n;
                }
            }
            return nullptr;
        };

    // The shape a real rejected rankings query came back with
    const char* rejected = R"({"data":{"worldData":{"encounter":{
        "name":"Sszorak",
        "characterRankings":{"error":"Invalid class and spec specified."}}}}})";
    rapidjson::Document bad;
    bad.Parse(rejected);
    REQUIRE_FALSE(bad.HasParseError());
    const char* found = findError(bad);
    REQUIRE(found != nullptr);
    CHECK(std::string(found) == "Invalid class and spec specified.");

    // A good reply must not trip it, or every pull would look broken
    const char* accepted = R"({"data":{"worldData":{"encounter":{
        "name":"Sszorak",
        "characterRankings":{"rankings":[{"name":"Someone","amount":1234.5}]}}}}})";
    rapidjson::Document good;
    good.Parse(accepted);
    REQUIRE_FALSE(good.HasParseError());
    CHECK(findError(good) == nullptr);
}

// WCL writes class names without spaces and answers anything else with
// "Invalid class and spec specified." Its own reports spell them the same way
// in masterData.actors.subType.
TEST_CASE("class names are sent the way the api spells them", "[wcl][naming]") {
    CHECK(std::string(parser::wclClassName(577)) == "DemonHunter");   // Havoc
    CHECK(std::string(parser::wclClassName(250)) == "DeathKnight");   // Blood
    CHECK(std::string(parser::wclClassName(71)) == "Warrior");        // Arms

    // The display name keeps its space - both spellings have to exist
    CHECK(std::string(parser::getClassName(577)) == "Demon Hunter");
}

#include "Models/RankingSample.h"
#include "Database/AnalysisPackage/CsvUtil.h"

// Parses the rankings replies a real pull produced. These are the only examples
// of this shape anywhere - nothing had ever queried characterRankings before -
// so the parser is written against them rather than against a guess.
TEST_CASE("ranking replies parse into fetchable pulls", "[wcl][sampler]") {
    const std::string dir = std::string(WCL_CACHE_DIR) + "/rankings";
    if (!fs::exists(dir)) { WARN("no rankings pulled yet; skipping"); return; }

    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    if (files.empty()) { WARN("rankings directory is empty; skipping"); return; }

    warcraftlogs::RankingSample sample;
    int parsed = 0;
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

        // The filename leads with the encounter id
        const int32_t encounterId = std::atoi(file.filename().string().c_str());

        const std::string err =
            warcraftlogs::parseRankingReply(json, encounterId, sample);
        INFO("file: " << file.filename().string());
        CHECK(err.empty());
        if (err.empty()) ++parsed;
    }

    UNSCOPED_INFO("files parsed: " << parsed << " of " << files.size());
    UNSCOPED_INFO("pulls: " << sample.pulls.size());
    UNSCOPED_INFO("empty encounters: " << sample.emptyEncounters.size());
    UNSCOPED_INFO("class/spec: " << sample.className << "/" << sample.specName);

    REQUIRE(parsed == static_cast<int>(files.size()));
    REQUIRE_FALSE(sample.pulls.empty());

    // The whole point: every pull has to name a fight that can actually be
    // fetched, or the sample cannot be turned into packages
    for (const auto& pull : sample.pulls) {
        INFO("pull by " << pull.playerName);
        CHECK_FALSE(pull.reportCode.empty());
        CHECK(pull.fightId > 0);
        CHECK(pull.encounterId > 0);
        CHECK(pull.amount > 0.0);
        CHECK(pull.durationMs > 0);
    }

    // Class and spec come off the entries, and must match what was queried
    CHECK(sample.className == "DemonHunter");
    CHECK(sample.specName == "Havoc");

    // Player names are not ASCII. One of these broke a console print while the
    // shape was being read, and these strings end up in a package.
    const bool anyNonAscii = std::any_of(
        sample.pulls.begin(), sample.pulls.end(), [](const auto& p) {
            return std::any_of(p.playerName.begin(), p.playerName.end(),
                               [](unsigned char c) { return c > 0x7F; });
        });
    UNSCOPED_INFO("sample contains non-ascii player names: " << anyNonAscii);

    // Private profiles rank as "Anonymous". The pulled files contain eleven,
    // and none may reach a sample - there is no fight behind them to fetch.
    for (const auto& pull : sample.pulls) {
        CHECK(pull.playerName != "Anonymous");
    }
}

// A private profile ranks as "Anonymous" with no report behind it. Counting
// those would inflate the sample with entries nothing can be fetched for.
TEST_CASE("anonymous rankings are left out", "[wcl][sampler]") {
    const char* reply = R"({"data":{"worldData":{"encounter":{
        "name":"Test",
        "characterRankings":{"page":1,"hasMorePages":false,"count":3,"rankings":[
          {"name":"Anonymous","amount":100.0,"duration":1000,"class":"DemonHunter","spec":"Havoc",
           "report":{"code":"anon99","fightID":3}},
          {"name":"Real","amount":200.0,"duration":2000,"class":"DemonHunter","spec":"Havoc",
           "report":{"code":"abc123","fightID":7}},
          {"name":"NoReport","amount":150.0,"duration":1500,"class":"DemonHunter","spec":"Havoc"}
        ]}}}}})";

    warcraftlogs::RankingSample sample;
    CHECK(warcraftlogs::parseRankingReply(reply, 42, sample).empty());

    REQUIRE(sample.pulls.size() == 1);
    CHECK(sample.pulls[0].playerName == "Real");
    CHECK(sample.pulls[0].reportCode == "abc123");
    CHECK(sample.pulls[0].fightId == 7);
}

// An encounter nobody has ranked is not a failure - it is a boss that has not
// been killed on this spec. A baseline should be able to say which.
TEST_CASE("an empty encounter is recorded, not treated as an error",
          "[wcl][sampler]") {
    const char* reply = R"({"data":{"worldData":{"encounter":{
        "name":"Kith'ix",
        "characterRankings":{"page":1,"hasMorePages":false,"count":0,"rankings":[]}}}}})";

    warcraftlogs::RankingSample sample;
    CHECK(warcraftlogs::parseRankingReply(reply, 3513, sample).empty());
    CHECK(sample.pulls.empty());
    REQUIRE(sample.emptyEncounters.size() == 1);
    CHECK(sample.emptyEncounters[0] == "Kith'ix");
}

// The error-in-a-200 case again, this time at the parser rather than the
// transport, so a stale cached error cannot be read back as a valid sample.
TEST_CASE("a refused query is reported by the parser too", "[wcl][sampler]") {
    const char* reply = R"({"data":{"worldData":{"encounter":{
        "name":"Sszorak",
        "characterRankings":{"error":"Invalid class and spec specified."}}}}})";

    warcraftlogs::RankingSample sample;
    const std::string err = warcraftlogs::parseRankingReply(reply, 3420, sample);
    CHECK_FALSE(err.empty());
    CHECK(err.find("Invalid class and spec") != std::string::npos);
    CHECK(sample.pulls.empty());
}

// A quarter of ranked players have non-ascii names, and those strings end up
// in package tables. The csv writer quotes on commas and quotes, not on byte
// values, so multibyte text has to pass through untouched.
TEST_CASE("non-ascii player names survive a csv field", "[wcl][sampler]") {
    // Real names from the pulled rankings
    const std::vector<std::string> names = {
        "\xE5\xB0\x8F\xE5\x82\x91\xE5\xAF\x8C\xE5\x8A\x9B\xE5\xA3\xAB",  // 小傑富力士
        "Ar\xC3\xA9s",                                                    // Arés
        "\xD0\x96\xD0\xB5\xD0\xBB\xD0\xB5\xD0\xB7\xD0\xBE",              // Железо
    };

    for (const auto& name : names) {
        const std::string field = awow::analysis::csvField(name);
        INFO("name bytes: " << name.size());
        // Nothing in these needs quoting, so the field must come back as-is -
        // a writer that escaped by byte value would corrupt them
        CHECK(field == name);
    }

    // And one that does need quoting, to prove the rule still applies to
    // multibyte text rather than being skipped for it
    const std::string withComma = "\xE5\xB0\x8F\xE5\x82\x91, the Swift";
    const std::string quoted = awow::analysis::csvField(withComma);
    CHECK(quoted.front() == '"');
    CHECK(quoted.back() == '"');
    CHECK(quoted.find(withComma) != std::string::npos);
}

#include "Adapter/WclToAwowAdapter.h"

// Warcraft Logs sends no "miss" event. An attack that did not land arrives as
// a damage event with a non-landing hitType and no amount, so the adapter's
// old dispatch on eventType == "miss" could never fire and defense.csv came
// out empty for every imported fight.
TEST_CASE("non-landing hit types become misses", "[wcl][miss]") {
    using warcraftlogs::WclToAwowAdapter;

    // Established from the cached reports, not from documentation
    CHECK(WclToAwowAdapter::missTypeFromHitType(0) == MissType::MISS);
    CHECK(WclToAwowAdapter::missTypeFromHitType(7) == MissType::DODGE);
    CHECK(WclToAwowAdapter::missTypeFromHitType(8) == MissType::DODGE);
    CHECK(WclToAwowAdapter::missTypeFromHitType(10) == MissType::IMMUNE);

    // Hits, crits and partial absorbs landed, so they are damage rather than
    // misses. Treating 4 as a miss would drop real damage on the floor - it
    // carries an amount and is 87% absorbed rather than avoided.
    CHECK(WclToAwowAdapter::missTypeFromHitType(1) == MissType::Unknown);
    CHECK(WclToAwowAdapter::missTypeFromHitType(2) == MissType::Unknown);
    CHECK(WclToAwowAdapter::missTypeFromHitType(4) == MissType::Unknown);

    // An unseen value must not be guessed into a miss
    CHECK(WclToAwowAdapter::missTypeFromHitType(99) == MissType::Unknown);
}

// The distribution the mapping was read off. If a future report shows a
// non-landing hitType carrying damage, or a landing one without it, the
// reasoning behind the mapping no longer holds and this says so.
TEST_CASE("the hit type evidence still holds", "[wcl][miss]") {
    const auto pages = cachedEventPages(25);
    if (pages.empty()) { WARN("no cached WCL pages; skipping"); return; }

    struct Stat { int n = 0; int zeroAmount = 0; };
    std::map<int, Stat> byHitType;

    for (const auto& page : pages) {
        rapidjson::Document doc;
        if (!loadJson(page, doc)) continue;
        for (const auto& ev : doc.GetArray()) {
            if (!ev.HasMember("type") || !ev["type"].IsString()) continue;
            if (std::string(ev["type"].GetString()) != "damage") continue;

            const int hitType = intOr(ev, "hitType", 1);
            auto& stat = byHitType[hitType];
            ++stat.n;
            if (intOr(ev, "amount") == 0) ++stat.zeroAmount;
        }
    }

    REQUIRE_FALSE(byHitType.empty());

    for (const auto& [hitType, stat] : byHitType) {
        const bool isMiss =
            warcraftlogs::WclToAwowAdapter::missTypeFromHitType(hitType)
                != MissType::Unknown;
        const int zeroPercent = stat.zeroAmount * 100 / stat.n;

        UNSCOPED_INFO("hitType " << hitType << ": " << stat.n << " events, "
                      << zeroPercent << "% with no amount, "
                      << (isMiss ? "treated as a miss" : "treated as damage"));

        // Every hit type mapped to a miss has to be one that never carries
        // damage, or the mapping is throwing real numbers away
        if (isMiss) CHECK(zeroPercent == 100);

        // The reverse is not asserted. hitType 9 turned up once, with no
        // amount and no absorb - structurally a non-landing hit - but one
        // event is not enough to say which miss it is, and a guess here would
        // put a wrong label in a package rather than leave a gap. It stays
        // classified as damage until a report shows enough of them to tell.
        if (!isMiss && zeroPercent == 100) {
            WARN("hitType " << hitType << " never carries damage (" << stat.n
                 << " events) but is not mapped to a miss - worth revisiting "
                 "if it becomes common");
        }
    }
}

// The point of the mapping: an imported fight should now produce misses where
// it produced none. Runs against a cached report, so it costs no queries.
TEST_CASE("an imported fight has misses in it", "[wcl][miss]") {
    const std::string root = WCL_CACHE_DIR;
    if (root.empty() || !fs::exists(root)) { WARN("no wcl cache; skipping"); return; }

    // Find a report whose pages contain a non-landing hit, so the test is
    // measuring the conversion rather than a fight where nothing was avoided
    fs::path chosen;
    std::error_code ec;
    for (const auto& report : fs::directory_iterator(root, ec)) {
        if (!report.is_directory()) continue;
        for (const auto& fight : fs::directory_iterator(report.path(), ec)) {
            if (!fight.is_directory()) continue;
            const auto page = fight.path() / "page1.json";
            if (!fs::exists(page)) continue;

            rapidjson::Document doc;
            if (!loadJson(page, doc)) continue;
            for (const auto& ev : doc.GetArray()) {
                if (!ev.HasMember("type") || !ev["type"].IsString()) continue;
                if (std::string(ev["type"].GetString()) != "damage") continue;
                const int hitType = intOr(ev, "hitType", 1);
                if (warcraftlogs::WclToAwowAdapter::missTypeFromHitType(hitType)
                        != MissType::Unknown) {
                    chosen = page;
                    break;
                }
            }
            if (!chosen.empty()) break;
        }
        if (!chosen.empty()) break;
    }

    if (chosen.empty()) { WARN("no cached page contains a miss; skipping"); return; }

    // Count what the page holds, then what the mapping makes of it
    rapidjson::Document doc;
    REQUIRE(loadJson(chosen, doc));

    std::map<MissType, int> expected;
    for (const auto& ev : doc.GetArray()) {
        if (!ev.HasMember("type") || !ev["type"].IsString()) continue;
        if (std::string(ev["type"].GetString()) != "damage") continue;
        const auto miss = warcraftlogs::WclToAwowAdapter::missTypeFromHitType(
            intOr(ev, "hitType", 1));
        if (miss != MissType::Unknown) ++expected[miss];
    }

    REQUIRE_FALSE(expected.empty());
    int total = 0;
    for (const auto& [type, count] : expected) total += count;

    UNSCOPED_INFO("page: " << chosen.string());
    UNSCOPED_INFO("misses the mapping finds: " << total);
    for (const auto& [type, count] : expected) {
        UNSCOPED_INFO("  type " << static_cast<int>(type) << ": " << count);
    }

    // Before the mapping this was zero for every imported fight, because the
    // adapter waited on a "miss" event type WarcraftLogs does not send
    CHECK(total > 0);
}

#include "OutputWriter.h"

// Haste ratings are checked with IsNumber, so a rating written as a double
// has to be read as one. Reading it with GetInt asserted in debug builds and
// gave garbage in release ones.
TEST_CASE("combatant info haste ratings sent as doubles are read", "[wcl][combatant]") {
    warcraftlogs::WclToAwowAdapter adapter;
    const auto metadata = adapter.parseReportMetadata(
        R"({"actors": [{"id": 1, "name": "Tester", "type": "Player"}]})");

    const auto data = adapter.convertEvents(
        R"([{"type": "combatantinfo", "timestamp": 0, "sourceID": 1,
             "hasteMelee": 1234.0, "hasteRanged": 567, "hasteSpell": 89.0}])",
        metadata, 0);

    REQUIRE(data.combatantInfoEvents.size() == 1);
    const auto& info = data.combatantInfoEvents.front();
    CHECK(info.haste_melee == 1234);
    CHECK(info.haste_ranged == 567);
    CHECK(info.haste_spell == 89);
}
