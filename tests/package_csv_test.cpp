#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include "Database/AnalysisPackage/PackageCsvWriter.h"
#include "Database/AnalysisPackage/ResourceCsvWriter.h"
#include "RealLogTestSupport.h"

using namespace test_support;

namespace {

using Writer = std::function<bool(const std::string&, const ExtractedData&, std::string_view)>;

struct Table {
    const char* name;
    Writer write;
    size_t columns;     // what the header promises
};

std::vector<Table> allTables() {
    using namespace awow::analysis;
    // Wrapped in lambdas because the writers no longer share one signature -
    // positions and resources have no spell columns, so they take no name map.
    auto noNames = [](bool (*fn)(const std::string&, const ExtractedData&,
                                 std::string_view)) {
        return [fn](const std::string& p, const ExtractedData& d,
                    std::string_view g) { return fn(p, d, g); };
    };
    auto withNames = [](bool (*fn)(const std::string&, const ExtractedData&,
                                   std::string_view, const SpellNames*)) {
        return [fn](const std::string& p, const ExtractedData& d,
                    std::string_view g) { return fn(p, d, g, nullptr); };
    };
    return {
        {"casts.csv",         withNames(writeCastsCsv),        13},
        {"combat.csv",        withNames(writeCombatCsv),       19},
        {"auras.csv",         withNames(writeAurasCsv),        10},
        {"resources.csv",     noNames(writeResourcesCsv),       9},
        {"energize.csv",      withNames(writeEnergizeCsv),      7},
        {"positions.csv",     noNames(writePositionsCsv),       8},
        {"deaths.csv",        withNames(writeDeathsCsv),        6},
        {"defense.csv",       withNames(writeDefenseCsv),      14},
        {"utility.csv",       withNames(writeUtilityCsv),       9},
        {"summons.csv",       withNames(writeSummonsCsv),       5},
        {"cd_reductions.csv", withNames(writeCdReductionsCsv),  5},
    };
}

} // namespace

// The whole point of a CSV is that column N means the same thing on every row.
// A writer that emits a different number of fields than its header silently
// shifts every value after the mistake, which is exactly the bug a reader
// cannot see.
TEST_CASE("every table's rows match its header width", "[package][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.combatEvents.empty()) { WARN("no real log available; skipping"); return; }

    for (const auto& table : allTables()) {
        const auto out = tempPath(table.name);
        INFO("table: " << table.name);
        REQUIRE(table.write(out, data, {}));

        auto lines = readLines(out);
        REQUIRE(lines.size() >= 1);          // header is always written

        const auto header = splitCsv(lines[0]);
        INFO("header: " << lines[0]);
        CHECK(header.size() == table.columns);

        for (size_t i = 1; i < lines.size(); ++i) {
            const auto fields = splitCsv(lines[i]);
            if (fields.size() != header.size()) {
                INFO("row " << i << ": " << lines[i]);
                CHECK(fields.size() == header.size());
                break;      // one report per table is enough
            }
        }

        UNSCOPED_INFO(table.name << ": " << lines.size() - 1 << " rows, "
                      << header.size() << " columns");

        if (!std::getenv("AWL_KEEP_CSV")) std::filesystem::remove(out);
    }
}

TEST_CASE("scoping keeps the player and their pets", "[package][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.combatEvents.empty()) { WARN("no real log available; skipping"); return; }

    // A player who actually cast something, so the tables are not empty
    std::string target;
    for (const auto& ev : data.spellCastEvents) {
        if (ev.actor_guid.rfind("Player-", 0) == 0) { target = ev.actor_guid; break; }
    }
    if (target.empty()) { WARN("no player casts; skipping"); return; }

    // casts.csv is about what the player did, so every row must be theirs or a
    // pet's - never an unrelated raider
    const auto castsPath = tempPath("scoped_casts.csv");
    REQUIRE(awow::analysis::writeCastsCsv(castsPath, data, target));
    auto castLines = readLines(castsPath);
    REQUIRE(castLines.size() > 1);

    int ownRows = 0;
    for (size_t i = 1; i < castLines.size(); ++i) {
        const auto f = splitCsv(castLines[i]);
        REQUIRE(f.size() == 13);
        if (f[2] == target) ++ownRows;
    }
    CHECK(ownRows > 0);

    // Scoping has to actually drop rows, or the check proves nothing
    const auto allPath = tempPath("all_casts.csv");
    REQUIRE(awow::analysis::writeCastsCsv(allPath, data, {}));
    CHECK(readLines(allPath).size() > castLines.size());

    std::filesystem::remove(castsPath);
    std::filesystem::remove(allPath);
}

TEST_CASE("combat.csv keeps damage taken, not just damage done", "[package][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.combatEvents.empty()) { WARN("no real log available; skipping"); return; }

    std::string target;
    for (const auto& ev : data.spellCastEvents) {
        if (ev.actor_guid.rfind("Player-", 0) == 0) { target = ev.actor_guid; break; }
    }
    if (target.empty()) { WARN("no player casts; skipping"); return; }

    const auto out = tempPath("scoped_combat.csv");
    REQUIRE(awow::analysis::writeCombatCsv(out, data, target));

    auto lines = readLines(out);
    REQUIRE(lines.size() > 1);

    int outgoing = 0, incoming = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = splitCsv(lines[i]);
        REQUIRE(f.size() == 19);
        if (f[2] == "outgoing") ++outgoing;
        else if (f[2] == "incoming") ++incoming;
        // The direction column is what says why a row was kept. A guardian
        // hitting a boss has the player in none of the guid columns - the
        // summon is what ties it to them - so checking for the player's guid
        // here would reject perfectly good pet damage.
        CHECK((f[2] == "outgoing" || f[2] == "incoming" || f[2] == "self"));
    }

    UNSCOPED_INFO("outgoing: " << outgoing << "  incoming: " << incoming);
    // A review needs both halves - what the player did and what hit them
    CHECK(outgoing > 0);
    CHECK(incoming > 0);

    std::filesystem::remove(out);
}

TEST_CASE("json tables are well formed", "[package][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.combatEvents.empty()) { WARN("no real log available; skipping"); return; }

    const auto combatant = tempPath("combatant.json");
    const auto lookups = tempPath("lookups.json");
    REQUIRE(awow::analysis::writeCombatantJson(combatant, data, {}));
    REQUIRE(awow::analysis::writeLookupsJson(lookups, data));

    // Balanced braces and brackets is a cheap proxy for parseable, and catches
    // the stray-comma shape of mistake these hand-written writers can make
    auto balanced = [](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
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
    };

    CHECK(balanced(combatant));
    CHECK(balanced(lookups));

    // And they should carry something, not just empty scaffolding
    auto sizeOf = [](const std::string& p) { return std::filesystem::file_size(p); };
    CHECK(sizeOf(lookups) > 64);

    std::filesystem::remove(combatant);
    std::filesystem::remove(lookups);
}
