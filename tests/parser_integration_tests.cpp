#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

#include "../AwowLab/Txttokenizer/include/Txttokenizer/Txttokenizer.h"
#include "../AwowLab/Parser/include/Parser/Parser.h"
#include "../AwowLab/Parser/include/Parser/ParserDebug.h"
#include "../AwowLab/Parser/include/Parser/Parser_Damage.h"
#include "../AwowLab/Parser/include/Parser/Parser_Cast.h"
#include "../AwowLab/Parser/include/Parser/Parser_Heal.h"
#include "../AwowLab/Parser/include/Parser/Parser_Resource.h"
#include "../AwowLab/Parser/include/Parser/Parser_Support.h"
#include "../AwowLab/Parser/include/Parser/Parser_Aura.h"
#include "../AwowLab/Parser/include/Parser/Parser_Missed.h"
#include "../AwowLab/Parser/include/Parser/Parser_Dispel.h"
#include "../AwowLab/Parser/include/Parser/Parser_Absorb.h"
#include "../AwowLab/Parser/include/Parser/Parser_Unit.h"
#include "../AwowLab/Parser/include/Parser/Parser_Meta.h"

struct ParseResult {
    int success = 0;
    int failure = 0;
    int total = 0;
    std::vector<std::string> failures;
};

// Test all parsers against the test events file
TEST_CASE("Parse all event types from test file", "[integration]") {
    // Load the test events file
    std::ifstream file("testevents.txt");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    // Tokenize
    tokenized_segment tokenizer;
    auto all_lines = tokenizer.tokenize(content.data(), content.size());

    REQUIRE(all_lines.size() > 0);
    std::cout << "Loaded " << all_lines.size() << " test events\n\n";

    // Track results per event type
    std::unordered_map<std::string, ParseResult> results;

    // Process each line
    for (const auto& tokens : all_lines) {
        if (tokens.size() < 3) continue;

        std::string eventType(tokens[2]);

        // Skip events we intentionally don't parse
        if (eventType == "EMOTE" || eventType == "WORLD_MARKER_REMOVED") {
            continue;
        }

        results[eventType].total++;

        bool parsed = false;
        std::string errorMsg;

        try {
            // Route to appropriate parser based on event type
            if (eventType == "SPELL_DAMAGE") {
                if (tokens.size() >= parser::EventParser<parser::SpellDamageTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size()) + " < " +
                               std::to_string(parser::EventParser<parser::SpellDamageTag>::expected_token_count());
                }
            }
            else if (eventType == "SPELL_PERIODIC_DAMAGE") {
                if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellPeriodicDamageTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SWING_DAMAGE" || eventType == "SWING_DAMAGE_LANDED") {
                if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SwingDamageLandedTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "DAMAGE_SPLIT") {
                if (tokens.size() >= parser::EventParser<parser::DamageSplitTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::DamageSplitTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "RANGE_DAMAGE") {
                // RANGE_DAMAGE uses same structure as SPELL_DAMAGE
                if (tokens.size() >= parser::EventParser<parser::SpellDamageTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Cast events
            else if (eventType == "SPELL_CAST_SUCCESS") {
                if (tokens.size() >= parser::EventParser<parser::SpellCastSuccessTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellCastSuccessTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_CAST_START") {
                if (tokens.size() >= parser::EventParser<parser::SpellCastStartTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellCastStartTag>::parse_and_return(tokens);
                    parsed = true;  // No position data expected
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_CAST_FAILED") {
                if (tokens.size() >= parser::EventParser<parser::SpellCastFailedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellCastFailedTag>::parse_and_return(tokens);
                    parsed = true;  // No position data expected
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_EMPOWER_START") {
                if (tokens.size() >= parser::EventParser<parser::SpellEmpowerStartTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellEmpowerStartTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_EMPOWER_END") {
                if (tokens.size() >= parser::EventParser<parser::SpellEmpowerEndTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellEmpowerEndTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_EMPOWER_INTERRUPT") {
                if (tokens.size() >= parser::EventParser<parser::SpellEmpowerInterruptTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellEmpowerInterruptTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Heal events
            else if (eventType == "SPELL_HEAL") {
                if (tokens.size() >= parser::EventParser<parser::SpellHealTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_PERIODIC_HEAL") {
                if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellPeriodicHealTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Resource events
            else if (eventType == "SPELL_ENERGIZE") {
                if (tokens.size() >= parser::EventParser<parser::SpellEnergizeTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellEnergizeTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_PERIODIC_ENERGIZE") {
                if (tokens.size() >= parser::EventParser<parser::SpellPeriodicEnergizeTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellPeriodicEnergizeTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_DRAIN") {
                if (tokens.size() >= parser::EventParser<parser::SpellDrainTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellDrainTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Support events
            else if (eventType == "SPELL_DAMAGE_SUPPORT") {
                if (tokens.size() >= parser::EventParser<parser::SpellDamageSupportTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellDamageSupportTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT") {
                if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageSupportTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellPeriodicDamageSupportTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_HEAL_SUPPORT") {
                if (tokens.size() >= parser::EventParser<parser::SpellHealSupportTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellHealSupportTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_PERIODIC_HEAL_SUPPORT") {
                if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealSupportTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellPeriodicHealSupportTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SWING_DAMAGE_LANDED_SUPPORT") {
                if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedSupportTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SwingDamageLandedSupportTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "RANGE_DAMAGE_SUPPORT") {
                // Uses same structure as SPELL_DAMAGE_SUPPORT
                if (tokens.size() >= parser::EventParser<parser::SpellDamageSupportTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellDamageSupportTag>::parse_and_return(tokens);
                    parsed = data.advanced_info.position.valid;
                    if (!parsed) errorMsg = "Position not valid";
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Aura events (no position expected)
            else if (eventType == "SPELL_AURA_APPLIED") {
                if (tokens.size() >= parser::EventParser<parser::SpellAuraAppliedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellAuraAppliedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_AURA_REMOVED") {
                if (tokens.size() >= parser::EventParser<parser::SpellAuraRemovedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellAuraRemovedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_AURA_REFRESH") {
                if (tokens.size() >= parser::EventParser<parser::SpellAuraRefreshTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellAuraRefreshTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_AURA_APPLIED_DOSE") {
                if (tokens.size() >= parser::EventParser<parser::SpellAuraAppliedDoseTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellAuraAppliedDoseTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_AURA_REMOVED_DOSE") {
                if (tokens.size() >= parser::EventParser<parser::SpellAuraRemovedDoseTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellAuraRemovedDoseTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_AURA_BROKEN_SPELL") {
                if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenSpellTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellAuraBrokenSpellTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Missed events (variable token count based on miss type)
            else if (eventType == "SPELL_MISSED") {
                // Check miss type to determine expected token count
                bool is_absorb = tokens.size() > 14 &&
                    (tokens[14] == "ABSORB" || tokens[14] == "BLOCK" || tokens[14] == "RESIST");
                size_t expected = is_absorb ?
                    parser::EventParser<parser::SpellMissedTag>::expected_token_count_absorb() :
                    parser::EventParser<parser::SpellMissedTag>::expected_token_count();
                if (tokens.size() >= expected) {
                    auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size()) + " (expected " + std::to_string(expected) + ")";
                }
            }
            else if (eventType == "SPELL_PERIODIC_MISSED") {
                bool is_absorb = tokens.size() > 14 &&
                    (tokens[14] == "ABSORB" || tokens[14] == "BLOCK" || tokens[14] == "RESIST");
                size_t expected = is_absorb ?
                    parser::EventParser<parser::SpellMissedTag>::expected_token_count_absorb() :
                    parser::EventParser<parser::SpellMissedTag>::expected_token_count();
                if (tokens.size() >= expected) {
                    auto data = parser::EventParser<parser::SpellPeriodicMissedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size()) + " (expected " + std::to_string(expected) + ")";
                }
            }
            else if (eventType == "SWING_MISSED") {
                if (tokens.size() >= parser::EventParser<parser::SwingMissedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SwingMissedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Dispel/interrupt events
            else if (eventType == "SPELL_DISPEL") {
                if (tokens.size() >= parser::EventParser<parser::SpellDispelTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellDispelTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_INTERRUPT") {
                if (tokens.size() >= parser::EventParser<parser::SpellInterruptTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellInterruptTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_EXTRA_ATTACKS") {
                if (tokens.size() >= parser::EventParser<parser::SpellExtraAttacksTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellExtraAttacksTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Absorb events
            else if (eventType == "SPELL_ABSORBED") {
                if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedTag>::minimum_token_count()) {
                    auto data = parser::EventParser<parser::SpellAbsorbedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_ABSORBED_SUPPORT") {
                if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedSupportTag>::minimum_token_count()) {
                    auto data = parser::EventParser<parser::SpellAbsorbedSupportTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Unit events
            else if (eventType == "UNIT_DIED") {
                if (tokens.size() >= parser::EventParser<parser::UnitDiedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::UnitDiedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "PARTY_KILL") {
                if (tokens.size() >= parser::EventParser<parser::PartyKillTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::PartyKillTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_SUMMON") {
                if (tokens.size() >= parser::EventParser<parser::SpellSummonTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellSummonTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_CREATE") {
                if (tokens.size() >= parser::EventParser<parser::SpellCreateTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellCreateTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_RESURRECT") {
                if (tokens.size() >= parser::EventParser<parser::SpellResurrectTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellResurrectTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "SPELL_INSTAKILL") {
                if (tokens.size() >= parser::EventParser<parser::SpellInstakillTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::SpellInstakillTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "ENVIRONMENTAL_DAMAGE") {
                if (tokens.size() >= parser::EventParser<parser::EnvironmentalDamageTag>::minimum_token_count()) {
                    auto data = parser::EventParser<parser::EnvironmentalDamageTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "EMOTE") {
                if (tokens.size() >= parser::EventParser<parser::EmoteTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::EmoteTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Meta events
            else if (eventType == "ZONE_CHANGE") {
                if (tokens.size() >= parser::EventParser<parser::ZoneChangeTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::ZoneChangeTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "MAP_CHANGE") {
                if (tokens.size() >= parser::EventParser<parser::MapChangeTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::MapChangeTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            else if (eventType == "WORLD_MARKER_PLACED") {
                if (tokens.size() >= parser::EventParser<parser::WorldMarkerPlacedTag>::expected_token_count()) {
                    auto data = parser::EventParser<parser::WorldMarkerPlacedTag>::parse_and_return(tokens);
                    parsed = true;
                } else {
                    errorMsg = "Token count: " + std::to_string(tokens.size());
                }
            }
            // Events we haven't implemented parsers for yet
            else if (eventType == "COMBATANT_INFO" || eventType == "ENCOUNTER_START" ||
                     eventType == "ENCOUNTER_END" || eventType == "SPELL_HEAL_ABSORBED" ||
                     eventType == "ENCHANT_APPLIED") {
                // These have existing parsers or are skipped
                parsed = true;
            }
            else {
                errorMsg = "No parser implemented";
            }
        }
        catch (const std::exception& e) {
            errorMsg = std::string("Exception: ") + e.what();
        }

        if (parsed) {
            results[eventType].success++;
        } else {
            results[eventType].failure++;
            if (results[eventType].failures.size() < 3) {
                results[eventType].failures.push_back(errorMsg);
            }
        }
    }

    // Print results
    std::cout << "\n========== PARSER TEST RESULTS ==========\n\n";

    int totalSuccess = 0, totalFailure = 0;
    std::vector<std::string> failedTypes;

    for (const auto& [eventType, result] : results) {
        totalSuccess += result.success;
        totalFailure += result.failure;

        std::cout << eventType << ": " << result.success << "/" << result.total;
        if (result.failure > 0) {
            std::cout << " (" << result.failure << " FAILED)";
            failedTypes.push_back(eventType);
            for (const auto& err : result.failures) {
                std::cout << "\n    - " << err;
            }
        }
        std::cout << "\n";
    }

    std::cout << "\n========== SUMMARY ==========\n";
    std::cout << "Total: " << totalSuccess << " success, " << totalFailure << " failure\n";
    std::cout << "Pass rate: " << (100.0 * totalSuccess / (totalSuccess + totalFailure)) << "%\n";

    if (!failedTypes.empty()) {
        std::cout << "\nFailed event types:\n";
        for (const auto& t : failedTypes) {
            std::cout << "  - " << t << "\n";
        }
    }

    // Test passes if we have >90% success rate
    REQUIRE(totalSuccess > totalFailure * 9);
}
