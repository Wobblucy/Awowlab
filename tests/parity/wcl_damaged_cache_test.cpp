#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Storage/ResponseCache.h"
#include "Adapter/WclToAwowAdapter.h"
#include "Adapter/WclJsonHelpers.h"

namespace fs = std::filesystem;

namespace {

// A cache folder holding one report whose metadata file is the given text.
struct DamagedCache {
    fs::path dir = fs::temp_directory_path() / "awowlab_damaged_wcl_cache";
    std::string code = "DAMAGED1";

    explicit DamagedCache(const std::string& meta) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / code);
        std::ofstream out(dir / code / "report_meta.json", std::ios::binary);
        out << meta;
    }
    ~DamagedCache() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("Actor id keys that are not numbers are rejected", "[wcl][cache]") {
    rapidjson::Document doc;
    doc.Parse(R"({"12": 0, "-3": 0, "abc": 0, "": 0, "7x": 0, "99999999999": 0})");
    REQUIRE(doc.IsObject());

    auto key = [&](const char* name) {
        return warcraftlogs::json::parseIdKey(doc.FindMember(name)->name);
    };
    CHECK(key("12") == std::optional<int32_t>(12));
    CHECK(key("-3") == std::optional<int32_t>(-3));
    CHECK_FALSE(key("abc"));
    CHECK_FALSE(key(""));
    CHECK_FALSE(key("7x"));
    CHECK_FALSE(key("99999999999"));
}

TEST_CASE("A damaged cached report loads without throwing", "[wcl][cache]") {
    SECTION("bad actor_map entries are skipped") {
        DamagedCache cache(R"({
            "fights": [{"id": 1, "name": "Boss"}, 5],
            "actors": [{"id": 4, "name": "Someone", "petOwner": "x"}, "junk"],
            "actor_map": {"4": "Someone", "abc": "Bad key", "9": 17, "": "Empty"}
        })");
        warcraftlogs::ResponseCache responseCache(cache.dir);

        std::optional<warcraftlogs::Report> report;
        REQUIRE_NOTHROW(report = responseCache.loadReport(cache.code));
        REQUIRE(report);
        CHECK(report->fights.size() == 1);
        REQUIRE(report->actors.size() == 1);
        CHECK(report->actors[0].petOwner == -1);
        CHECK(report->actorMap.size() == 1);
        CHECK(report->actorMap[4] == "Someone");
    }

    SECTION("a file that is JSON but not an object") {
        DamagedCache cache("[1, 2, 3]");
        warcraftlogs::ResponseCache responseCache(cache.dir);
        std::optional<warcraftlogs::Report> report;
        REQUIRE_NOTHROW(report = responseCache.loadReport(cache.code));
        CHECK_FALSE(report);
    }
}

TEST_CASE("Report metadata skips actor_map entries it cannot read", "[wcl][adapter]") {
    warcraftlogs::WclToAwowAdapter adapter;
    warcraftlogs::WclReportMetadata metadata;
    REQUIRE_NOTHROW(metadata = adapter.parseReportMetadata(std::string(
        R"({"report_code": "X", "actor_map": {"5": "Five", "abc": "Bad", "6": 6}})")));
    CHECK(metadata.actorIdToName.size() == 1);
    CHECK(metadata.actorIdToName[5] == "Five");
}
