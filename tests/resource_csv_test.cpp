#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include "Database/AnalysisPackage/ResourceCsvWriter.h"
#include "RealLogTestSupport.h"

using namespace test_support;

TEST_CASE("resources.csv carries every power type", "[analysis][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.resourceEvents.empty()) { WARN("no real log available; skipping"); return; }

    const auto out = tempPath("awl_resources.csv");
    REQUIRE(awow::analysis::writeResourcesCsv(out, data));

    auto lines = readLines(out);
    REQUIRE(lines.size() > 1);

    const auto header = splitCsv(lines[0]);
    REQUIRE(header.size() == 9);
    CHECK(header[0] == "timestamp_ms");
    CHECK(header[5] == "power_slot");

    int secondaryRows = 0;
    int healthRows = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = splitCsv(lines[i]);
        REQUIRE(f.size() == header.size());

        const int slot = std::stoi(f[5]);
        CHECK(slot >= 0);
        if (slot > 0) {
            ++secondaryRows;
            // Health belongs to the event, not to each resource, so it is
            // written once on the primary row - a reader summing the health
            // column must not count a snapshot twice
            CHECK(f[3].empty());
            CHECK(f[4].empty());
        } else if (!f[3].empty()) {
            ++healthRows;
        }
        CHECK_FALSE(f[1].empty());
    }

    UNSCOPED_INFO("rows: " << lines.size() - 1
                  << "  secondary-resource rows: " << secondaryRows
                  << "  health rows: " << healthRows);
    CHECK(healthRows > 0);
    CHECK(secondaryRows > 0);

    if (!std::getenv("AWL_KEEP_CSV")) std::filesystem::remove(out);
}

TEST_CASE("energize.csv keeps fractional amounts and overflow", "[analysis][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.energizeEvents.empty()) { WARN("no real log available; skipping"); return; }

    const auto out = tempPath("awl_energize.csv");
    REQUIRE(awow::analysis::writeEnergizeCsv(out, data));

    auto lines = readLines(out);
    REQUIRE(lines.size() > 1);

    const auto header = splitCsv(lines[0]);
    REQUIRE(header.size() == 7);
    CHECK(header[6] == "overflow");

    int fractional = 0, withOverflow = 0, named = 0, drains = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = splitCsv(lines[i]);
        REQUIRE(f.size() == header.size());

        CHECK(std::stoul(f[2]) > 0);
        const double amount = std::stod(f[5]);
        const double overflow = std::stod(f[6]);
        CHECK(overflow >= 0.0);
        if (amount < 0.0) ++drains;
        if (overflow > 0.0) ++withOverflow;
        if (f[5].find(".0000") == std::string::npos) ++fractional;
        if (!f[3].empty()) ++named;
    }

    UNSCOPED_INFO("rows: " << lines.size() - 1 << "  fractional: " << fractional
                  << "  with overflow: " << withOverflow
                  << "  named spells: " << named << "  drains: " << drains);
    // Exactly the values an integer parse would have flattened
    CHECK(fractional > 0);
    CHECK(withOverflow > 0);
    CHECK(named > 0);

    if (!std::getenv("AWL_KEEP_CSV")) std::filesystem::remove(out);
}

TEST_CASE("scoping to a player drops other actors", "[analysis][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.energizeEvents.empty()) { WARN("no real log available; skipping"); return; }

    const std::string target = data.energizeEvents.front().actor_guid;
    REQUIRE_FALSE(target.empty());

    const auto scoped = tempPath("awl_energize_scoped.csv");
    REQUIRE(awow::analysis::writeEnergizeCsv(scoped, data, target));

    auto scopedLines = readLines(scoped);
    REQUIRE(scopedLines.size() > 1);
    for (size_t i = 1; i < scopedLines.size(); ++i) {
        CHECK(splitCsv(scopedLines[i])[1] == target);
    }

    // Scoping has to actually remove rows, or passing the check proves nothing
    const auto all = tempPath("awl_energize_all.csv");
    REQUIRE(awow::analysis::writeEnergizeCsv(all, data));
    CHECK(readLines(all).size() > scopedLines.size());

    std::filesystem::remove(scoped);
    std::filesystem::remove(all);
}

TEST_CASE("local logs name every energize spell", "[analysis][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.energizeEvents.empty()) { WARN("no real log available; skipping"); return; }

    // The log writes the spell name inline on every energize line, so a bare
    // id in the export means the extractor is dropping it - only the WCL
    // adapter and snapshot importer used to fill spellNameMap.
    int missing = 0;
    for (const auto& ev : data.energizeEvents) {
        if (data.spellNameMap.find(ev.spell_id) == data.spellNameMap.end()) ++missing;
    }
    UNSCOPED_INFO("energize events without a name: " << missing
                  << " of " << data.energizeEvents.size());
    CHECK(missing == 0);
}

TEST_CASE("scoped export labels the target as the player", "[analysis][reallog]") {
    std::string buffer;
    auto data = extractFromRealLog(buffer);
    if (data.resourceEvents.empty()) { WARN("no real log available; skipping"); return; }

    // Pick a player that actually appears in the resource table
    std::string target;
    for (const auto& ev : data.resourceEvents) {
        if (ev.actor_type == ActorType::Player) { target = ev.actor_guid; break; }
    }
    if (target.empty()) { WARN("no player resource rows; skipping"); return; }

    const auto out = tempPath("awl_resources_scoped.csv");
    REQUIRE(awow::analysis::writeResourcesCsv(out, data, target));

    auto lines = readLines(out);
    REQUIRE(lines.size() > 1);

    int playerRows = 0, enemyRows = 0, otherRows = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = splitCsv(lines[i]);
        REQUIRE(f.size() == 9);
        if (f[2] == "player") { ++playerRows; CHECK(f[1] == target); }
        else if (f[2] == "enemy") ++enemyRows;
        else ++otherRows;
    }

    UNSCOPED_INFO("player rows: " << playerRows << "  enemy rows: " << enemyRows
                  << "  other rows: " << otherRows);
    CHECK(playerRows > 0);
    // Scoped exports keep boss health for context but nothing else
    CHECK(otherRows == 0);

    std::filesystem::remove(out);
}

TEST_CASE("commas in names stay inside one field", "[analysis]") {
    ExtractedData data;
    EnergizeEvent ev;
    ev.actor_guid = "Player-1,with-comma";
    ev.timestamp_ms = 100;
    ev.spell_id = 42;
    ev.power_type = 6;
    ev.amount = 1.5;
    ev.overflow = 0.25;
    data.energizeEvents.push_back(ev);
    data.spellNameMap[42] = "Spell, With Comma";

    const auto out = tempPath("awl_quoting.csv");
    REQUIRE(awow::analysis::writeEnergizeCsv(out, data));

    auto lines = readLines(out);
    REQUIRE(lines.size() == 2);
    const auto f = splitCsv(lines[1]);
    REQUIRE(f.size() == 7);
    CHECK(f[1] == "Player-1,with-comma");
    CHECK(f[3] == "Spell, With Comma");
    CHECK(f[5] == "1.5000");
    CHECK(f[6] == "0.2500");

    std::filesystem::remove(out);
}
