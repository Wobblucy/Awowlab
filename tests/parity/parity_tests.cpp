#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "parity_test_helpers.h"
#include "../ShippedHasteBuffs.h"
#include "OutputWriter.h"
#include "Adapter/WclToAwowAdapter.h"
#include "Txttokenizer/Txttokenizer.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_set>
#include <rapidjson/document.h>

namespace fs = std::filesystem;

namespace {

const std::string FIXTURE_DIR = "fixtures/parity/";

std::string loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ExtractedData parseLocalFixture(const std::string& txt_path) {
    std::string content = loadFile(txt_path);

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());

    return OutputWriter::extractAllEvents(lines);
}

ExtractedData parseWclFixture(
    const std::string& json_path,
    const std::string& metadata_path
) {
    std::string meta_content = loadFile(metadata_path);
    rapidjson::Document metadata;
    metadata.Parse(meta_content.c_str());

    uint64_t fightStartTime = 0;
    if (metadata.HasMember("fight_start_time_wcl")) {
        fightStartTime = metadata["fight_start_time_wcl"].GetUint64();
    }

    std::string events_content = loadFile(json_path);
    rapidjson::Document events;
    events.Parse(events_content.c_str());

    if (!events.IsArray()) {
        throw std::runtime_error("Events JSON must be an array");
    }

    warcraftlogs::WclReportMetadata reportMeta;

    if (metadata.HasMember("actor_mapping") && metadata["actor_mapping"].IsObject()) {
        const auto& mapping = metadata["actor_mapping"];

        for (auto it = mapping.MemberBegin(); it != mapping.MemberEnd(); ++it) {
            if (!it->value.IsObject()) continue;

            int32_t wcl_id = 0;
            std::string name;
            std::string local_guid;

            if (it->value.HasMember("wcl_id") && it->value["wcl_id"].IsInt()) {
                wcl_id = it->value["wcl_id"].GetInt();
            }
            if (it->value.HasMember("name") && it->value["name"].IsString()) {
                name = it->value["name"].GetString();
            }
            if (it->value.HasMember("local_guid") && it->value["local_guid"].IsString()) {
                local_guid = it->value["local_guid"].GetString();
            }

            if (wcl_id > 0) {
                reportMeta.actorIdToName[wcl_id] = name;

                // Use the real GUID when the fixture knows it: production
                // metadata from WCL masterData carries real GUIDs, and the
                // adapter's friendly-fire rule keys off the Player- prefix.
                // Unmapped actors get a synthetic GUID.
                reportMeta.actorIdToGuid[wcl_id] = !local_guid.empty()
                    ? local_guid
                    : "Actor-0-0-0-0-" + std::to_string(wcl_id);
            }
        }
    }

    warcraftlogs::WclToAwowAdapter adapter;
    return adapter.convertEvents(events, reportMeta, fightStartTime);
}

bool fixtureFilesExist(const std::string& subdir) {
    std::string txt_path = FIXTURE_DIR + subdir + "/samples.txt";
    std::string json_path = FIXTURE_DIR + subdir + "/samples.json";
    std::string meta_path = FIXTURE_DIR + "metadata.json";

    return fs::exists(txt_path) && fs::exists(json_path) && fs::exists(meta_path);
}

std::vector<std::string> getAvailableFixtures() {
    std::vector<std::string> fixtures;
    std::string base_path = FIXTURE_DIR;

    if (!fs::exists(base_path)) return fixtures;

    for (const auto& entry : fs::directory_iterator(base_path)) {
        if (entry.is_directory()) {
            std::string name = entry.path().filename().string();
            if (fixtureFilesExist(name)) {
                fixtures.push_back(name);
            }
        }
    }
    return fixtures;
}

// Event listings for a failure message, so a mismatch can be read off
// without rerunning under a debugger.
std::string describeCombatEvents(const ExtractedData& local, const ExtractedData& wcl) {
    std::ostringstream out;
    auto list = [&out](const char* label, const ExtractedData& data) {
        out << label << " combat events: " << data.combatEvents.size() << "\n";
        for (size_t i = 0; i < data.combatEvents.size(); ++i) {
            const auto& e = data.combatEvents[i];
            out << "  " << label << "[" << i << "]: spell_id=" << e.spell_id
                << " ts=" << e.timestamp_ms
                << " amount=" << e.amount
                << " eff_amt=" << e.effective_amount
                << " absorbed=" << e.absorbed
                << " type=" << static_cast<int>(e.event_type)
                << " flags=0x" << std::hex << static_cast<int>(e.flags) << std::dec
                << "\n";
        }
    };
    list("Local", local);
    list("WCL", wcl);
    return out.str();
}

std::string describeAuraEvents(const ExtractedData& local, const ExtractedData& wcl) {
    std::ostringstream out;
    auto list = [&out](const char* label, const ExtractedData& data) {
        out << label << " aura events: " << data.auraEvents.size() << "\n";
        for (size_t i = 0; i < data.auraEvents.size(); ++i) {
            const auto& e = data.auraEvents[i];
            out << "  " << label << "[" << i << "]: spell_id=" << e.spell_id
                << " ts=" << e.timestamp_ms
                << " type=" << static_cast<int>(e.event_type)
                << " aura=" << static_cast<int>(e.aura_type)
                << " stacks=" << static_cast<int>(e.stacks)
                << " target=" << e.target_guid
                << "\n";
        }
    };
    list("Local", local);
    list("WCL", wcl);
    return out.str();
}

double combatMatchRate(const parity::ParityResult& result) {
    return result.combat_events_matched > 0
        ? static_cast<double>(result.combat_events_matched) /
          (result.combat_events_matched + result.combat_events_mismatched)
        : 0.0;
}

double auraMatchRate(const parity::ParityResult& result) {
    return result.aura_events_matched > 0
        ? static_cast<double>(result.aura_events_matched) /
          (result.aura_events_matched + result.aura_events_mismatched)
        : 0.0;
}

// Loads one fixture pair and checks the combat events match at minRate.
void checkCombatParity(const std::string& fixture, double minRate) {
    auto local = parseLocalFixture(FIXTURE_DIR + fixture + "/samples.txt");
    auto wcl = parseWclFixture(
        FIXTURE_DIR + fixture + "/samples.json",
        FIXTURE_DIR + "metadata.json"
    );

    auto guidMap = parity::loadGuidMapping(FIXTURE_DIR + "metadata.json");
    parity::ComparisonTolerances tol;
    parity::ParityResult result = parity::compareExtractedData(local, wcl, guidMap, tol);

    for (const auto& diff : result.differences) {
        UNSCOPED_INFO(diff);
    }
    INFO("Matched: " << result.combat_events_matched);
    INFO("Mismatched: " << result.combat_events_mismatched);
    INFO(describeCombatEvents(local, wcl));

    REQUIRE(combatMatchRate(result) >= minRate);
}

// The same for aura events.
void checkAuraParity(const std::string& fixture, double minRate) {
    auto local = parseLocalFixture(FIXTURE_DIR + fixture + "/samples.txt");
    auto wcl = parseWclFixture(
        FIXTURE_DIR + fixture + "/samples.json",
        FIXTURE_DIR + "metadata.json"
    );

    auto guidMap = parity::loadGuidMapping(FIXTURE_DIR + "metadata.json");
    parity::ComparisonTolerances tol;
    parity::ParityResult result = parity::compareExtractedData(local, wcl, guidMap, tol);

    for (const auto& diff : result.differences) {
        UNSCOPED_INFO(diff);
    }
    INFO("Matched: " << result.aura_events_matched);
    INFO("Mismatched: " << result.aura_events_mismatched);
    INFO(describeAuraEvents(local, wcl));

    REQUIRE(auraMatchRate(result) >= minRate);
}

} // anonymous namespace

TEST_CASE("SPELL_DAMAGE events produce equivalent ExtractedData", "[parity][damage][SPELL_DAMAGE]") {
    if (!fixtureFilesExist("SPELL_DAMAGE")) {
        SKIP("SPELL_DAMAGE fixtures not found - run fixture_generator first");
    }

    SECTION("Combat event counts are similar") {
        auto local = parseLocalFixture(FIXTURE_DIR + "SPELL_DAMAGE/samples.txt");
        auto wcl = parseWclFixture(
            FIXTURE_DIR + "SPELL_DAMAGE/samples.json",
            FIXTURE_DIR + "metadata.json"
        );

        size_t local_count = local.combatEvents.size();
        size_t wcl_count = wcl.combatEvents.size();

        INFO("Local combat events: " << local_count);
        INFO("WCL combat events: " << wcl_count);

        size_t tolerance = std::max(size_t(2), local_count / 10);
        REQUIRE(std::abs((int)local_count - (int)wcl_count) <= (int)tolerance);
    }

    SECTION("Matched damage events have equivalent amounts") {
        checkCombatParity("SPELL_DAMAGE", 0.90);
    }
}

TEST_CASE("SPELL_PERIODIC_DAMAGE events produce equivalent ExtractedData", "[parity][damage][SPELL_PERIODIC_DAMAGE]") {
    if (!fixtureFilesExist("SPELL_PERIODIC_DAMAGE")) {
        SKIP("SPELL_PERIODIC_DAMAGE fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_PERIODIC_DAMAGE", 0.90);
}

TEST_CASE("SWING_DAMAGE_LANDED events produce equivalent ExtractedData", "[parity][damage][SWING_DAMAGE_LANDED]") {
    if (!fixtureFilesExist("SWING_DAMAGE_LANDED")) {
        SKIP("SWING_DAMAGE_LANDED fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SWING_DAMAGE_LANDED", 0.90);
}

TEST_CASE("SPELL_HEAL events produce equivalent ExtractedData", "[parity][healing][SPELL_HEAL]") {
    if (!fixtureFilesExist("SPELL_HEAL")) {
        SKIP("SPELL_HEAL fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_HEAL", 0.90);
}

TEST_CASE("SPELL_PERIODIC_HEAL events produce equivalent ExtractedData", "[parity][healing][SPELL_PERIODIC_HEAL]") {
    if (!fixtureFilesExist("SPELL_PERIODIC_HEAL")) {
        SKIP("SPELL_PERIODIC_HEAL fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_PERIODIC_HEAL", 0.90);
}

TEST_CASE("SPELL_AURA_APPLIED events produce equivalent ExtractedData", "[parity][aura][SPELL_AURA_APPLIED]") {
    if (!fixtureFilesExist("SPELL_AURA_APPLIED")) {
        SKIP("SPELL_AURA_APPLIED fixtures not found - run fixture_generator first");
    }
    checkAuraParity("SPELL_AURA_APPLIED", 0.85);
}

TEST_CASE("SPELL_AURA_REMOVED events produce equivalent ExtractedData", "[parity][aura][SPELL_AURA_REMOVED]") {
    if (!fixtureFilesExist("SPELL_AURA_REMOVED")) {
        SKIP("SPELL_AURA_REMOVED fixtures not found - run fixture_generator first");
    }
    checkAuraParity("SPELL_AURA_REMOVED", 0.85);
}

TEST_CASE("SPELL_CAST_SUCCESS events produce equivalent ExtractedData", "[parity][cast][SPELL_CAST_SUCCESS]") {
    if (!fixtureFilesExist("SPELL_CAST_SUCCESS")) {
        SKIP("SPELL_CAST_SUCCESS fixtures not found - run fixture_generator first");
    }

    auto local = parseLocalFixture(FIXTURE_DIR + "SPELL_CAST_SUCCESS/samples.txt");
    auto wcl = parseWclFixture(
        FIXTURE_DIR + "SPELL_CAST_SUCCESS/samples.json",
        FIXTURE_DIR + "metadata.json"
    );

    SECTION("Cast event counts are similar") {
        size_t local_count = local.spellCastEvents.size();
        size_t wcl_count = wcl.spellCastEvents.size();

        INFO("Local cast events: " << local_count);
        INFO("WCL cast events: " << wcl_count);

        // Wider than the damage tolerance: begincast/cast pairing differs
        // between the two sources.
        size_t tolerance = std::max(size_t(3), local_count / 5);
        REQUIRE(std::abs((int)local_count - (int)wcl_count) <= (int)tolerance);
    }
}

// SPELL_EMPOWER_END has no case of its own: it is handled as a cast success.

// The *_SUPPORT cases (Augmentation Evoker) allow more variance; the healing
// ones differ in overheal.
TEST_CASE("SPELL_DAMAGE_SUPPORT events produce equivalent ExtractedData", "[parity][damage][SPELL_DAMAGE_SUPPORT]") {
    if (!fixtureFilesExist("SPELL_DAMAGE_SUPPORT")) {
        SKIP("SPELL_DAMAGE_SUPPORT fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_DAMAGE_SUPPORT", 0.80);
}

TEST_CASE("SPELL_HEAL_SUPPORT events produce equivalent ExtractedData", "[parity][healing][SPELL_HEAL_SUPPORT]") {
    if (!fixtureFilesExist("SPELL_HEAL_SUPPORT")) {
        SKIP("SPELL_HEAL_SUPPORT fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_HEAL_SUPPORT", 0.80);
}

TEST_CASE("SPELL_PERIODIC_DAMAGE_SUPPORT events produce equivalent ExtractedData", "[parity][damage][SPELL_PERIODIC_DAMAGE_SUPPORT]") {
    if (!fixtureFilesExist("SPELL_PERIODIC_DAMAGE_SUPPORT")) {
        SKIP("SPELL_PERIODIC_DAMAGE_SUPPORT fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_PERIODIC_DAMAGE_SUPPORT", 0.80);
}

TEST_CASE("SPELL_PERIODIC_HEAL_SUPPORT events produce equivalent ExtractedData", "[parity][healing][SPELL_PERIODIC_HEAL_SUPPORT]") {
    if (!fixtureFilesExist("SPELL_PERIODIC_HEAL_SUPPORT")) {
        SKIP("SPELL_PERIODIC_HEAL_SUPPORT fixtures not found - run fixture_generator first");
    }
    checkCombatParity("SPELL_PERIODIC_HEAL_SUPPORT", 0.80);
}

TEST_CASE("Full parity check across all available event types", "[parity][full]") {
    auto fixtures = getAvailableFixtures();

    if (fixtures.empty()) {
        SKIP("No fixtures found - run fixture_generator first");
    }

    // Fixtures the two sources can't agree on, for reasons outside the parser:
    // - SWING_DAMAGE_LANDED_SUPPORT: the WCL fixture is broken (no supportID,
    //   every amount 0)
    // - DAMAGE_SPLIT: the event means different things in each source
    // - SPELL_AURA_BROKEN_SPELL: the aura type is encoded differently
    // - "damage"/"healing": old folder names for SPELL_DAMAGE/SPELL_HEAL
    static const std::unordered_set<std::string> skip_fixtures = {
        "SWING_DAMAGE_LANDED_SUPPORT",
        "DAMAGE_SPLIT",
        "SPELL_AURA_BROKEN_SPELL",
        "damage",
        "healing"
    };

    auto guidMap = parity::loadGuidMapping(FIXTURE_DIR + "metadata.json");
    parity::ComparisonTolerances tol;
    parity::ParityResult combined_result;

    INFO("Found " << fixtures.size() << " fixture directories");

    for (const auto& fixture : fixtures) {
        if (skip_fixtures.count(fixture)) {
            continue;
        }

        INFO("Processing fixture: " << fixture);

        try {
            auto local = parseLocalFixture(FIXTURE_DIR + fixture + "/samples.txt");
            auto wcl = parseWclFixture(FIXTURE_DIR + fixture + "/samples.json", FIXTURE_DIR + "metadata.json");
            auto result = parity::compareExtractedData(local, wcl, guidMap, tol);

            combined_result.combat_events_matched += result.combat_events_matched;
            combined_result.combat_events_mismatched += result.combat_events_mismatched;
            combined_result.aura_events_matched += result.aura_events_matched;
            combined_result.aura_events_mismatched += result.aura_events_mismatched;
            combined_result.death_events_matched += result.death_events_matched;
            combined_result.death_events_mismatched += result.death_events_mismatched;

            if (result.combat_events_mismatched > 0 || result.aura_events_mismatched > 0) {
                UNSCOPED_INFO("[MISMATCH] " << fixture
                              << " combat: " << result.combat_events_matched << "/" << result.combat_events_mismatched
                              << " aura: " << result.aura_events_matched << "/" << result.aura_events_mismatched);
            }
        } catch (const std::exception& e) {
            WARN("Failed to process fixture " << fixture << ": " << e.what());
        }
    }

    double match_rate = combined_result.getMatchRate();
    INFO("Overall match rate: " << (match_rate * 100) << "%");
    INFO("Combat matched: " << combined_result.combat_events_matched);
    INFO("Combat mismatched: " << combined_result.combat_events_mismatched);
    INFO("Aura matched: " << combined_result.aura_events_matched);
    INFO("Aura mismatched: " << combined_result.aura_events_mismatched);
    INFO("Death matched: " << combined_result.death_events_matched);
    INFO("Death mismatched: " << combined_result.death_events_mismatched);

    REQUIRE(match_rate >= 0.85);
}
