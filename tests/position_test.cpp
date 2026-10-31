#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

#include "Txttokenizer/Txttokenizer.h"
#include "Parser/Parser.h"
#include "Parser/Parser_Damage.h"
#include "Parser/Parser_Cast.h"
#include "structures.h"

int main(int argc, char* argv[]) {
    // Manual spot-check against a real combat log. Pass the log path as
    // the first argument, or set AWOWLAB_REAL_LOG.
    const char* envLog = std::getenv("AWOWLAB_REAL_LOG");
    std::string filepath = (argc > 1) ? argv[1] : (envLog ? envLog : "");
    if (filepath.empty()) {
        std::cout << "SKIPPED: no combat log given\n";
        std::cout << "Usage: position_test <path-to-WoWCombatLog.txt>\n";
        return 0;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        // No log to check against - that's a skip, not a failure. The
        // automated suites don't ship a real log for this tool.
        std::cout << "SKIPPED: no combat log at " << filepath << "\n";
        std::cout << "Usage: position_test <path-to-WoWCombatLog.txt>\n";
        return 0;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    std::cout << "Loaded " << content.size() << " bytes\n";

    // Tokenize
    tokenized_segment tokenizer;
    auto all_lines = tokenizer.tokenize(content.data(), content.size());

    std::cout << "Tokenized " << all_lines.size() << " lines\n\n";

    // Test position parsing for various event types
    int spell_cast_count = 0;
    int spell_damage_count = 0;
    int valid_positions = 0;
    int invalid_positions = 0;

    // Sample some events to verify positions look reasonable
    std::cout << "=== Sample Position Data ===\n\n";

    for (size_t i = 0; i < all_lines.size() && spell_cast_count < 10; i++) {
        const auto& tokens = all_lines[i];
        if (tokens.size() < 3) continue;

        std::string_view eventType = tokens[2];

        if (eventType == "SPELL_CAST_SUCCESS") {
            if (tokens.size() >= parser::EventParser<parser::SpellCastSuccessTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellCastSuccessTag>::parse_and_return(tokens);

                if (data.advanced_info.position.valid) {
                    float x = data.advanced_info.position.x_loc / 100.0f;
                    float y = data.advanced_info.position.y_loc / 100.0f;

                    // WoW coordinates are typically in range 0-5000
                    bool reasonable = (std::abs(x) < 10000 && std::abs(y) < 10000 &&
                                       (x != 0 || y != 0));

                    if (spell_cast_count < 5) {
                        std::cout << "SPELL_CAST_SUCCESS: " << data.spell.spell_name
                                  << " pos=(" << x << ", " << y << ")"
                                  << " map=" << data.advanced_info.position.map_id
                                  << (reasonable ? " [OK]" : " [SUSPICIOUS]") << "\n";
                    }

                    if (reasonable) valid_positions++;
                    else invalid_positions++;

                    spell_cast_count++;
                }
            }
        }
    }

    std::cout << "\n";

    for (size_t i = 0; i < all_lines.size() && spell_damage_count < 10; i++) {
        const auto& tokens = all_lines[i];
        if (tokens.size() < 3) continue;

        std::string_view eventType = tokens[2];

        if (eventType == "SPELL_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

                if (data.advanced_info.position.valid) {
                    float x = data.advanced_info.position.x_loc / 100.0f;
                    float y = data.advanced_info.position.y_loc / 100.0f;

                    bool reasonable = (std::abs(x) < 10000 && std::abs(y) < 10000 &&
                                       (x != 0 || y != 0));

                    if (spell_damage_count < 5) {
                        std::cout << "SPELL_DAMAGE: " << data.spell.spell_name
                                  << " amount=" << data.amount
                                  << " pos=(" << x << ", " << y << ")"
                                  << " map=" << data.advanced_info.position.map_id
                                  << (reasonable ? " [OK]" : " [SUSPICIOUS]") << "\n";
                    }

                    if (reasonable) valid_positions++;
                    else invalid_positions++;

                    spell_damage_count++;
                }
            }
        }
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "SPELL_CAST_SUCCESS events with position: " << spell_cast_count << "\n";
    std::cout << "SPELL_DAMAGE events with position: " << spell_damage_count << "\n";
    std::cout << "Valid positions (reasonable coords): " << valid_positions << "\n";
    std::cout << "Invalid/suspicious positions: " << invalid_positions << "\n";

    if (invalid_positions == 0 && valid_positions > 0) {
        std::cout << "\n*** POSITION PARSING LOOKS CORRECT! ***\n";
        return 0;
    } else if (invalid_positions > valid_positions) {
        std::cout << "\n*** WARNING: Many invalid positions - check token indices! ***\n";
        return 1;
    }

    return 0;
}
