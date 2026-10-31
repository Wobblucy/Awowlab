#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "Txttokenizer/Txttokenizer.h"
#include "Database/OutputWriter.h"
#include "Database/AnalysisPackage/PackageBuilder.h"
#include "Core/SpellNameDatabase.h"
#include "Encounter/EncounterScanner.h"
#include "RealLogTestSupport.h"

extern "C" {
#include "miniz.h"
}

namespace fs = std::filesystem;

using test_support::extractFromRealLog;

namespace {

// Reads up to `size` bytes of the log from the line holding its first
// ENCOUNTER_START, trimmed to whole lines. COMBATANT_INFO lines are written at
// encounter start, which in a long log can be far from the top of the file.
// Empty when the log has no encounter.
std::string readFromFirstEncounter(std::ifstream& in, size_t size) {
    std::streamoff lineStart = 0;
    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (line.find("ENCOUNTER_START") != std::string::npos) {
            found = true;
            break;
        }
        lineStart += static_cast<std::streamoff>(line.size()) + 1;
    }
    if (!found) return {};

    in.clear();
    in.seekg(lineStart);
    std::string buffer(size, '\0');
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    buffer.resize(static_cast<size_t>(in.gcount()));

    const size_t lastNewline = buffer.rfind('\n');
    if (lastNewline == std::string::npos) return {};
    buffer.resize(lastNewline + 1);
    return buffer;
}

// Reads the archive back with the same library that wrote it, so the test
// proves the zip is openable rather than just present on disk.
std::set<std::string> zipEntries(const std::string& zipPath) {
    std::set<std::string> names;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return names;

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        names.emplace(stat.m_filename);
    }
    mz_zip_reader_end(&zip);
    return names;
}

std::string zipEntryText(const std::string& zipPath, const char* entry) {
    std::string text;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return text;

    size_t size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(&zip, entry, &size, 0);
    if (p) {
        text.assign(static_cast<const char*>(p), size);
        mz_free(p);
    }
    mz_zip_reader_end(&zip);
    return text;
}

bool jsonBalanced(const std::string& text) {
    int braces = 0, brackets = 0;
    bool inString = false, escaped = false;
    for (char c : text) {
        if (escaped) { escaped = false; continue; }
        if (inString) {
            if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') ++braces;
        else if (c == '}') --braces;
        else if (c == '[') ++brackets;
        else if (c == ']') --brackets;
        if (braces < 0 || brackets < 0) return false;
    }
    return braces == 0 && brackets == 0 && !inString && !text.empty();
}

fs::path scratchDir(const char* name) {
    auto dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

awow::analysis::FightData makeFight(const ExtractedData& data,
                                    const std::string& playerGuid) {
    awow::analysis::FightData fight;
    fight.data = &data;
    fight.info.fightKey = "01_test_fight";
    fight.info.sourceName = "WoWCombatLog-test.txt";
    fight.info.fightId = 1;
    fight.info.encounterId = 3129;
    fight.info.encounterName = "Midnight Falls";
    fight.info.difficultyId = 16;
    fight.info.kill = true;
    fight.info.durationMs = 312000;
    fight.info.playerGuid = playerGuid;
    fight.info.playerName = "Testplayer";
    fight.info.specId = 250;      // Blood DK
    fight.info.gameBuild = "12.1.0";
    return fight;
}

} // namespace

TEST_CASE("a package zip opens and holds every promised file", "[builder][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.combatEvents.empty()) { WARN("no real log available; skipping"); return; }

    std::string target;
    for (const auto& ev : data.spellCastEvents) {
        if (ev.actor_guid.rfind("Player-", 0) == 0) { target = ev.actor_guid; break; }
    }
    if (target.empty()) { WARN("no player casts; skipping"); return; }

    const auto outDir = scratchDir("awl_pkg_out");
    awow::analysis::PackageRequest request;
    request.type = awow::analysis::PackageType::ReviewEncounter;
    request.source = awow::analysis::PackageSource::LocalLog;
    request.notes = "test package, with a comma and a \"quote\"";

    const std::vector<awow::analysis::FightData> fights{makeFight(data, target)};
    const auto result = awow::analysis::buildPackage(outDir.string(), request, fights);

    INFO("error: " << result.error);
    REQUIRE(result.ok);
    REQUIRE(fs::exists(result.zipPath));
    CHECK(result.zipBytes > 0);

    const auto entries = zipEntries(result.zipPath);
    REQUIRE_FALSE(entries.empty());

    // Every file the spec lists, at the path the spec puts it
    const std::vector<std::string> expected = {
        "manifest.json",
        "fights/01_test_fight/casts.csv",
        "fights/01_test_fight/combat.csv",
        "fights/01_test_fight/auras.csv",
        "fights/01_test_fight/resources.csv",
        "fights/01_test_fight/energize.csv",
        "fights/01_test_fight/positions.csv",
        "fights/01_test_fight/deaths.csv",
        "fights/01_test_fight/defense.csv",
        "fights/01_test_fight/utility.csv",
        "fights/01_test_fight/summons.csv",
        "fights/01_test_fight/cd_reductions.csv",
        "fights/01_test_fight/combatant.json",
        "fights/01_test_fight/lookups.json",
    };
    for (const auto& name : expected) {
        INFO("missing entry: " << name);
        CHECK(entries.count(name) == 1);
    }
    CHECK(result.fileCount == expected.size());

    // The manifest has to survive the round trip through the archive
    const auto manifest = zipEntryText(result.zipPath, "manifest.json");
    REQUIRE_FALSE(manifest.empty());
    CHECK(jsonBalanced(manifest));
    CHECK(manifest.find("\"package_version\": 1") != std::string::npos);
    CHECK(manifest.find("\"encounter_name\": \"Midnight Falls\"") != std::string::npos);
    CHECK(manifest.find("\"kill\": true") != std::string::npos);
    CHECK(manifest.find("\"spec_name\": \"Blood\"") != std::string::npos);
    CHECK(manifest.find("\"source\": \"local\"") != std::string::npos);
    // A quote in the notes must be escaped, not left to break the json
    CHECK(manifest.find("\\\"quote\\\"") != std::string::npos);
    // The compiled-in version fills in when the caller leaves it blank
    CHECK(manifest.find("\"app_version\": \"\"") == std::string::npos);

    // A table pulled out of the zip should still be a readable csv
    const auto casts = zipEntryText(result.zipPath, "fights/01_test_fight/casts.csv");
    REQUIRE_FALSE(casts.empty());
    CHECK(casts.rfind("start_ms,end_ms,", 0) == 0);

    std::error_code ec;
    fs::remove_all(outDir, ec);
}

TEST_CASE("the zip name follows the spec", "[builder]") {
    ExtractedData data;
    awow::analysis::PackageRequest request;
    request.type = awow::analysis::PackageType::BaselineSample;

    awow::analysis::FightData fight;
    fight.data = &data;
    fight.info.specId = 250;   // Blood
    const std::vector<awow::analysis::FightData> fights{fight};

    const auto name = awow::analysis::packageFileName(request, fights);
    INFO("name: " << name);
    CHECK(name.rfind("baseline_sample_blood_", 0) == 0);
    CHECK(name.size() > std::string("baseline_sample_blood_").size() + 4);
    CHECK(name.substr(name.size() - 4) == ".zip");

    // A spec we do not know leaves that piece out rather than writing "unknown"
    awow::analysis::FightData unknownSpec;
    unknownSpec.data = &data;
    const auto bare = awow::analysis::packageFileName(
        request, std::vector<awow::analysis::FightData>{unknownSpec});
    INFO("bare: " << bare);
    CHECK(bare.rfind("baseline_sample_", 0) == 0);
    CHECK(bare.find("unknown") == std::string::npos);
}

TEST_CASE("a bad request fails without leaving a zip behind", "[builder]") {
    ExtractedData data;
    const auto outDir = scratchDir("awl_pkg_fail");
    awow::analysis::PackageRequest request;

    // No fights at all
    auto result = awow::analysis::buildPackage(outDir.string(), request, {});
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());

    // A fight with no data attached
    awow::analysis::FightData empty;
    empty.info.fightKey = "broken";
    result = awow::analysis::buildPackage(outDir.string(), request, {empty});
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("broken") != std::string::npos);

    // An apl path that does not exist should stop the build, not be skipped
    awow::analysis::FightData fight = makeFight(data, "");
    request.aplPath = (outDir / "nope.simc").string();
    result = awow::analysis::buildPackage(outDir.string(), request, {fight});
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("apl") != std::string::npos);

    // None of those attempts may leave an archive lying around
    int zips = 0;
    for (const auto& entry : fs::directory_iterator(outDir)) {
        if (entry.path().extension() == ".zip") ++zips;
    }
    CHECK(zips == 0);

    std::error_code ec;
    fs::remove_all(outDir, ec);
}

TEST_CASE("sampling metadata only appears for baseline packages", "[builder]") {
    ExtractedData data;
    awow::analysis::FightData fight;
    fight.data = &data;
    fight.info.fightKey = "f";
    const std::vector<awow::analysis::FightData> fights{fight};

    awow::analysis::PackageRequest plain;
    const auto without = awow::analysis::buildManifestJson(plain, fights);
    CHECK(without.find("\"sampling\"") == std::string::npos);
    CHECK(jsonBalanced(without));

    awow::analysis::PackageRequest sampled;
    sampled.type = awow::analysis::PackageType::BaselineSample;
    sampled.sampling.present = true;
    sampled.sampling.percentile = 95.0;
    sampled.sampling.stratum = "mythic";
    sampled.sampling.rankingMetric = "dps";
    const auto with = awow::analysis::buildManifestJson(sampled, fights);
    CHECK(with.find("\"sampling\"") != std::string::npos);
    CHECK(with.find("\"stratum\": \"mythic\"") != std::string::npos);
    CHECK(jsonBalanced(with));
}

TEST_CASE("unfilled manifest fields say null rather than zero", "[builder]") {
    ExtractedData data;
    awow::analysis::FightData fight;
    fight.data = &data;
    fight.info.fightKey = "f";
    const std::vector<awow::analysis::FightData> fights{fight};

    awow::analysis::PackageRequest request;
    const auto manifest = awow::analysis::buildManifestJson(request, fights);

    // Nothing parses hero tree or item level yet. Writing 0 would read as a
    // real value; null says the data was not collected.
    CHECK(manifest.find("\"hero_tree\": null") != std::string::npos);
    CHECK(manifest.find("\"item_level\": null") != std::string::npos);
    CHECK(jsonBalanced(manifest));
}

// The first 40MB of a long log can land mid-pull, past every COMBATANT_INFO
// line, so this reads from the first encounter instead.
TEST_CASE("combatant.json carries spec and talents", "[builder][reallog]") {
    const std::string path = REAL_LOG_PATH;
    if (path.empty()) { WARN("AWOWLAB_REAL_LOG not set; skipping"); return; }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) { WARN("real log not readable; skipping"); return; }

    const std::string buffer = readFromFirstEncounter(in, 20u * 1024u * 1024u);
    if (buffer.empty()) { WARN("no encounter in the real log; skipping"); return; }

    tokenized_segment seg;
    auto lines = seg.tokenize(buffer.data(), buffer.size());
    auto data = OutputWriter::extractAllEvents(lines);

    if (data.combatantInfoEvents.empty()) {
        WARN("no COMBATANT_INFO in this slice; skipping");
        return;
    }

    const std::string target = data.combatantInfoEvents.front().player_guid;
    const auto outDir = scratchDir("awl_pkg_combatant");

    awow::analysis::PackageRequest request;
    auto fight = makeFight(data, target);
    fight.info.specId = data.combatantInfoEvents.front().spec_id;
    const auto result =
        awow::analysis::buildPackage(outDir.string(), request, {fight});
    INFO("error: " << result.error);
    REQUIRE(result.ok);

    const auto json =
        zipEntryText(result.zipPath, "fights/01_test_fight/combatant.json");
    REQUIRE_FALSE(json.empty());
    CHECK(jsonBalanced(json));

    UNSCOPED_INFO("combatant info events: " << data.combatantInfoEvents.size());
    UNSCOPED_INFO("json bytes: " << json.size());

    // The scoped player has to actually be in there, with a real spec
    CHECK(json.find(target) != std::string::npos);
    CHECK(json.find("\"spec_id\": 0") == std::string::npos);
    CHECK(json.find("\"talents\"") != std::string::npos);

    std::error_code ec;
    fs::remove_all(outDir, ec);
}

// The cleanup-on-failure branch only runs once the archive is already open, and
// none of the earlier checks reach it - they all bail out first. Pointing the
// apl at a directory gets past the existence check and then fails on read,
// which is the only way in.
TEST_CASE("a failure after the zip opens leaves nothing behind", "[builder]") {
    ExtractedData data;
    const auto outDir = scratchDir("awl_pkg_midfail");

    const auto aplDir = outDir / "looks_like_a_file.simc";
    std::error_code ec;
    fs::create_directories(aplDir, ec);
    REQUIRE(fs::exists(aplDir));

    awow::analysis::PackageRequest request;
    request.aplPath = aplDir.string();
    auto fight = makeFight(data, "");

    const auto result = awow::analysis::buildPackage(outDir.string(), request, {fight});
    INFO("error: " << result.error);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());

    int zips = 0;
    for (const auto& entry : fs::directory_iterator(outDir)) {
        if (entry.path().extension() == ".zip") ++zips;
    }
    CHECK(zips == 0);

    fs::remove_all(outDir, ec);
}

// Spell names come from the shipped database, not from whatever the source
// happened to carry. Local logs name spells inline and WCL does not name them
// at all, so without this a WCL package exports bare ids where a local one
// exports names - the same schema meaning different things depending on where
// the fight came from.
TEST_CASE("the resolver names spells the log left bare", "[builder][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.combatEvents.empty()) { WARN("no real log available; skipping"); return; }

    auto& db = SpellNameDatabase::instance();
    db.setDataDirectory(SPELL_NAMES_DIR);

    const auto ids = data.collectAllSpellIds();
    REQUIRE_FALSE(ids.empty());
    const size_t loaded = db.loadSpellIds(ids);
    UNSCOPED_INFO("referenced spell ids: " << ids.size() << "  names loaded: " << loaded);

    // Stand in for a WCL import: strip every inline name so the only way a
    // column can get filled is through the resolver.
    ExtractedData bare = data;
    bare.spellNameMap.clear();
    for (auto& e : bare.auraEvents) e.spell_name.clear();
    for (auto& e : bare.missedEvents) e.spell_name.clear();

    awow::analysis::PackageRequest request;
    request.source = awow::analysis::PackageSource::Wcl;
    request.resolveSpellName = [&db](uint32_t id) -> std::string_view {
        return db.hasSpellName(id) ? std::string_view(db.getSpellName(id))
                                   : std::string_view{};
    };

    const auto outDir = scratchDir("awl_pkg_names");
    auto fight = makeFight(bare, "");
    const auto result = awow::analysis::buildPackage(outDir.string(), request, {fight});
    INFO("error: " << result.error);
    REQUIRE(result.ok);

    // Counts how many rows have a non-empty field at the given column
    auto namedAtColumn = [](const std::string& csv, size_t column) {
        int named = 0;
        std::istringstream in(csv);
        std::string line;
        std::getline(in, line);                      // header
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            size_t col = 0, start = 0;
            bool quoted = false;
            for (size_t i = 0; i <= line.size(); ++i) {
                const bool end = (i == line.size());
                if (!end && line[i] == '"') { quoted = !quoted; continue; }
                if (end || (line[i] == ',' && !quoted)) {
                    if (col == column) {
                        if (i > start) ++named;
                        break;
                    }
                    ++col;
                    start = i + 1;
                }
            }
        }
        return named;
    };

    const auto casts = zipEntryText(result.zipPath, "fights/01_test_fight/casts.csv");
    REQUIRE_FALSE(casts.empty());
    const int castsNamed = namedAtColumn(casts, 5);      // spell_name
    UNSCOPED_INFO("casts.csv rows with a name: " << castsNamed);
    CHECK(castsNamed > 0);

    // auras.csv had every inline name cleared, so anything here came from the
    // resolver and nowhere else
    const auto auras = zipEntryText(result.zipPath, "fights/01_test_fight/auras.csv");
    REQUIRE_FALSE(auras.empty());
    const int aurasNamed = namedAtColumn(auras, 6);      // spell_name
    UNSCOPED_INFO("auras.csv rows with a name: " << aurasNamed);
    CHECK(aurasNamed > 0);

    // And lookups.json must explain the ids the tables mention
    const auto lookups = zipEntryText(result.zipPath, "fights/01_test_fight/lookups.json");
    REQUIRE_FALSE(lookups.empty());
    CHECK(jsonBalanced(lookups));
    UNSCOPED_INFO("lookups.json bytes: " << lookups.size());
    CHECK(lookups.size() > 64);

    std::error_code ec;
    fs::remove_all(outDir, ec);
}

// Everything the manifest needs about an encounter already lives in the
// EncounterSegment the user picked, and the player side is in the events. The
// mapping exists so a caller does not have to restate either.
TEST_CASE("segment and events fill a FightInfo", "[mapping][reallog]") {
    const std::string logPath = REAL_LOG_PATH;
    if (logPath.empty()) { WARN("AWOWLAB_REAL_LOG not set; skipping"); return; }

    std::ifstream in(logPath, std::ios::binary);
    if (!in.is_open()) { WARN("real log not readable; skipping"); return; }

    const std::string buffer = readFromFirstEncounter(in, 30u * 1024u * 1024u);
    if (buffer.empty()) { WARN("no encounter in the real log; skipping"); return; }

    const auto segments = EncounterScanner::scanForEncountersLightweight(
        buffer.data(), buffer.size());
    if (segments.empty()) { WARN("no encounters in slice; skipping"); return; }

    tokenized_segment seg;
    auto lines = seg.tokenize(buffer.data(), buffer.size());
    auto data = OutputWriter::extractAllEvents(lines);
    if (data.combatantInfoEvents.empty()) { WARN("no combatant info; skipping"); return; }

    const std::string player = data.combatantInfoEvents.front().player_guid;

    // The slice can end partway through a pull, leaving a short fragment
    // beside the whole one. Take the longest so the duration check measures a
    // complete pull.
    const auto& s = *std::max_element(
        segments.begin(), segments.end(), [](const auto& a, const auto& b) {
            return (a.endTimestamp_ms - a.startTimestamp_ms) <
                   (b.endTimestamp_ms - b.startTimestamp_ms);
        });

    const auto info = awow::analysis::fightInfoFromSegment(
        s, data, player, 3, "WoWCombatLog-092426_211948.txt");

    UNSCOPED_INFO("segments found: " << segments.size());
    UNSCOPED_INFO("fightKey: " << info.fightKey);
    UNSCOPED_INFO("encounter: " << info.encounterName
                  << "  difficulty=" << info.difficultyId
                  << "  keystone=" << info.keystoneLevel
                  << "  kill=" << (info.kill ? "yes" : "no")
                  << "  duration=" << info.durationMs << "ms");
    UNSCOPED_INFO("player: " << info.playerName << " spec=" << info.specId);

    // The encounter side must come straight off the segment
    CHECK(info.encounterId == s.encounterId);
    CHECK(info.encounterName == s.encounterName);
    CHECK(info.difficultyId == s.difficultyId);
    CHECK(info.keystoneLevel == s.keystoneLevel);
    CHECK(info.kill == s.isSuccess);
    CHECK(info.durationMs == s.endTimestamp_ms - s.startTimestamp_ms);
    CHECK(info.durationMs > 1000);   // a real pull, not a sliced fragment

    // The player side comes from the events
    CHECK(info.playerGuid == player);
    CHECK(info.specId != 0);

    // Folder name sorts by pull order, not boss name
    CHECK(info.fightKey.rfind("03_", 0) == 0);
    CHECK(info.fightKey.find(' ') == std::string::npos);
    CHECK(info.sourceName == "WoWCombatLog-092426_211948.txt");

    // Not parsed anywhere yet - the mapping must not invent them
    CHECK(info.gameBuild.empty());
    CHECK(info.itemLevel == 0);

    // And it has to survive into the manifest
    const auto outDir = scratchDir("awl_pkg_mapping");
    awow::analysis::PackageRequest request;
    awow::analysis::FightData fight;
    fight.data = &data;
    fight.info = info;

    const auto result = awow::analysis::buildPackage(outDir.string(), request, {fight});
    INFO("error: " << result.error);
    REQUIRE(result.ok);

    const auto manifest = zipEntryText(result.zipPath, "manifest.json");
    REQUIRE_FALSE(manifest.empty());
    CHECK(jsonBalanced(manifest));
    CHECK(manifest.find("\"" + info.encounterName + "\"") != std::string::npos);
    CHECK(manifest.find("\"difficulty_name\"") != std::string::npos);
    CHECK(manifest.find("\"item_level\": null") != std::string::npos);

    // The fight folder is named by the key the mapping produced
    const auto entries = zipEntries(result.zipPath);
    CHECK(entries.count("fights/" + info.fightKey + "/casts.csv") == 1);

    std::error_code ec;
    fs::remove_all(outDir, ec);
}

TEST_CASE("a package builds under a folder the ANSI code page cannot spell",
          "[builder][unicode]") {
    // A profile folder such as C:\Users\<Cyrillic name> puts both the temp
    // folder the package is staged in and the chosen output folder outside
    // the code page. The folder picker hands the output folder over as UTF-8.
    const fs::path base = scratchDir("awl_pkg_unicode") / fs::path(u8"\u0418\u043c\u044f");
    std::error_code ec;
    fs::create_directories(base / "tmp", ec);

    const wchar_t* previous = _wgetenv(L"TMP");
    const std::wstring savedTmp = previous ? previous : L"";
    _wputenv_s(L"TMP", (base / "tmp").c_str());

    ExtractedData data;
    awow::analysis::PackageRequest request;
    const std::u8string outDir = (base / "out").u8string();
    const auto result = awow::analysis::buildPackage(
        std::string(outDir.begin(), outDir.end()), request, {makeFight(data, "")});

    _wputenv_s(L"TMP", savedTmp.c_str());

    INFO("error: " << result.error);
    REQUIRE(result.ok);
    const fs::path zip(std::u8string(result.zipPath.begin(), result.zipPath.end()));
    CHECK(fs::exists(zip));
    CHECK(zip.parent_path() == base / "out");
    CHECK(zipEntries(result.zipPath).count("manifest.json") == 1);

    fs::remove_all(base.parent_path(), ec);
}
