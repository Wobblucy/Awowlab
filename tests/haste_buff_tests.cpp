#include <catch2/catch_test_macros.hpp>

#include "ShippedHasteBuffs.h"
#include "Database/HasteBuffData.h"
#include "Haste/HasteTracker.h"

#include <filesystem>
#include <string>

namespace {

// The tracker reads the shared table, which the listener filled with the
// shipped file. A test that swaps in its own entries puts the shipped ones back.
class TableSwap {
public:
    TableSwap() : saved_(haste::HasteBuffTable::instance()) {
        haste::HasteBuffTable::instance().clear();
    }
    ~TableSwap() { haste::HasteBuffTable::instance() = saved_; }

    TableSwap(const TableSwap&) = delete;
    TableSwap& operator=(const TableSwap&) = delete;

private:
    haste::HasteBuffTable saved_;
};

const std::string kPlayer = "Player-1-00000001";

} // namespace

TEST_CASE("haste buff file entries are read with their stacking", "[haste]") {
    haste::HasteBuffTable table;
    const size_t loaded = awow::parseHasteBuffs(R"({
        "version": "12.1",
        "buffs": {
            "2825":   {"haste": 300, "stacks": 1, "name": "Bloodlust"},
            "279709": {"haste": 20, "stacks": 3, "name": "Starlord"},
            "1000001": {"haste": -100, "name": "a slow, stacks left out"}
        }
    })", table);

    CHECK(loaded == 3);
    REQUIRE(table.find(2825));
    CHECK(table.find(2825)->haste_permille == 300);
    CHECK_FALSE(table.find(2825)->stacks());
    REQUIRE(table.find(279709));
    CHECK(table.find(279709)->max_stacks == 3);
    CHECK(table.find(279709)->stacks());
    REQUIRE(table.find(1000001));
    CHECK(table.find(1000001)->haste_permille == -100);
    CHECK(table.find(1000001)->max_stacks == 1);
    CHECK_FALSE(table.find(12472));
}

TEST_CASE("haste buff entries with a bad field are skipped, not fatal", "[haste]") {
    haste::HasteBuffTable table;
    const size_t loaded = awow::parseHasteBuffs(R"({
        "buffs": {
            "2825":  {"haste": 300},
            "abc":   {"haste": 100},
            "0":     {"haste": 100},
            "12472": {"haste": "20"},
            "10060": {"stacks": 2},
            "80353": 300
        }
    })", table);

    CHECK(loaded == 1);
    CHECK(table.size() == 1);
    CHECK(table.find(2825));
}

TEST_CASE("a file that is not a haste buff table leaves the table alone", "[haste]") {
    haste::HasteBuffTable table;
    table.add(2825, {300, 1});

    CHECK(awow::parseHasteBuffs("not json", table) == 0);
    CHECK(awow::parseHasteBuffs(R"({"entries": {}})", table) == 0);
    CHECK(awow::parseHasteBuffs(R"({"buffs": []})", table) == 0);
    CHECK(table.size() == 1);
}

TEST_CASE("the tracker applies a flat haste buff multiplicatively", "[haste]") {
    TableSwap swap;
    haste::HasteBuffTable::instance().add(2825, {300, 1});

    haste::HasteTracker tracker;
    tracker.setBaseHasteFromRating(kPlayer, 6600);  // 10%
    REQUIRE(tracker.getHasteAtTime(kPlayer, 0) == 100);

    CHECK(tracker.applyBuff(kPlayer, 2825, 1, 1000));
    // 1.10 * 1.30 = 1.43
    CHECK(tracker.getHasteAtTime(kPlayer, 1000) == 430);
    CHECK(tracker.getHasteAtTime(kPlayer, 999) == 100);

    // A non-stacking buff ignores stack updates; the log refreshes it, it does not stack
    CHECK_FALSE(tracker.updateBuffStacks(kPlayer, 2825, 3, 1500));
    CHECK(tracker.getHasteAtTime(kPlayer, 1500) == 430);

    CHECK(tracker.removeBuff(kPlayer, 2825, 2000));
    CHECK(tracker.getHasteAtTime(kPlayer, 2000) == 100);
}

TEST_CASE("the tracker counts a stacking haste buff once per stack", "[haste]") {
    TableSwap swap;
    haste::HasteBuffTable::instance().add(279709, {20, 3});

    haste::HasteTracker tracker;
    tracker.setBaseHasteFromRating(kPlayer, 0);

    CHECK(tracker.applyBuff(kPlayer, 279709, 1, 100));
    CHECK(tracker.getHasteAtTime(kPlayer, 100) == 20);
    CHECK(tracker.updateBuffStacks(kPlayer, 279709, 3, 200));
    CHECK(tracker.getHasteAtTime(kPlayer, 200) == 60);
    CHECK(tracker.updateBuffStacks(kPlayer, 279709, 2, 300));
    CHECK(tracker.getHasteAtTime(kPlayer, 300) == 40);
    CHECK(tracker.removeBuff(kPlayer, 279709, 400));
    CHECK(tracker.getHasteAtTime(kPlayer, 400) == 0);
}

TEST_CASE("auras missing from the table do not touch haste", "[haste]") {
    TableSwap swap;

    haste::HasteTracker tracker;
    tracker.setBaseHasteFromRating(kPlayer, 6600);
    CHECK_FALSE(tracker.applyBuff(kPlayer, 2825, 1, 1000));
    CHECK_FALSE(tracker.removeBuff(kPlayer, 2825, 2000));
    CHECK(tracker.getHasteAtTime(kPlayer, 1500) == 100);
}

// Guards the generated files themselves: a column shift in the export would
// still produce a table, just one where Bloodlust no longer reads 30%.
TEST_CASE("shipped haste buff tables hold the known buffs", "[haste][shipped]") {
    for (const char* version : {"12.0", "12.1"}) {
        const std::string path = std::string(HASTE_BUFFS_DIR) + "/haste_buffs_" + version + ".json";
        INFO("file: " << path);
        REQUIRE(std::filesystem::exists(path));

        TableSwap swap;
        REQUIRE(awow::loadHasteBuffs(path));
        const auto& table = haste::HasteBuffTable::instance();
        CHECK(table.size() > 200);

        for (uint32_t lust : {2825u, 32182u, 80353u, 264667u, 390386u}) {
            INFO("spell " << lust);
            REQUIRE(table.find(lust));
            CHECK(table.find(lust)->haste_permille == 300);
            CHECK_FALSE(table.find(lust)->stacks());
        }
        REQUIRE(table.find(26297));  // Berserking
        CHECK(table.find(26297)->haste_permille == 100);
        REQUIRE(table.find(279709));  // Starlord
        CHECK(table.find(279709)->max_stacks == 3);
        CHECK(table.find(343312));  // Furious Gaze, granted by a script
    }
}

TEST_CASE("the default spell data version ships a haste buff table", "[haste][shipped]") {
    // The listener loaded this one; an empty table here means every parsed
    // log in this run went without haste buffs.
    CHECK(std::filesystem::exists(test_support::shippedHasteBuffsPath()));
    CHECK_FALSE(haste::HasteBuffTable::instance().empty());
}
