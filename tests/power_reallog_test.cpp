#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include "Txttokenizer/Txttokenizer.h"
#include "Parser/Parser_Damage.h"
#include "Parser/Parser_Cast.h"

// Runs real log lines through the production tokenizer and parser to confirm
// the pipe-separated power fields survive the whole path. The hand-built token
// vectors in parser_unit_tests.cpp cannot catch a tokenizer-level problem
// (quoted actor names, the timezone suffix) the way this does.
TEST_CASE("real log multi-resource lines keep every resource", "[power][reallog]") {
    const std::string path = REAL_LOG_PATH;
    if (path.empty()) { WARN("AWOWLAB_REAL_LOG not set; skipping"); return; }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) { WARN("real log not readable; skipping"); return; }

    // Read a slice big enough to contain multi-resource casts
    std::string buffer(40u * 1024u * 1024u, '\0');
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    buffer.resize(static_cast<size_t>(in.gcount()));
    REQUIRE(buffer.size() > 0);

    // Trim to the last complete line so the tokenizer never sees a partial one
    size_t lastNewline = buffer.rfind('\n');
    REQUIRE(lastNewline != std::string::npos);
    buffer.resize(lastNewline + 1);

    tokenized_segment seg;
    auto lines = seg.tokenize(buffer.data(), buffer.size());
    REQUIRE(lines.size() > 0);

    int multiResource = 0;
    int singleResource = 0;
    std::map<std::string, int> patterns;

    for (const auto& tokens : lines) {
        if (tokens.size() < 3) continue;
        if (tokens[2] != "SPELL_CAST_SUCCESS") continue;
        if (tokens.size() < parser::EventParser<parser::SpellCastSuccessTag>::expected_token_count())
            continue;

        auto data = parser::EventParser<parser::SpellCastSuccessTag>::parse_and_return(tokens);
        const auto& info = data.advanced_info;
        if (info.power_count == 0) continue;

        if (info.power_count == 1) { ++singleResource; }
        else {
            ++multiResource;
            std::ostringstream key;
            for (uint8_t i = 0; i < info.power_count; ++i) {
                if (i) key << '|';
                key << static_cast<int>(info.powers[i].type);
            }
            patterns[key.str()]++;
        }

        // The flat fields must still mirror entry 0 - the cooldown reduction
        // lookup in OutputWriter keys on them
        CHECK(info.power_type == info.powers[0].type);
        CHECK(info.current_power == info.powers[0].current);
        CHECK(info.power_cost == info.powers[0].cost);
    }

    UNSCOPED_INFO("single-resource casts: " << singleResource);
    UNSCOPED_INFO("multi-resource casts:  " << multiResource);
    for (const auto& [pat, n] : patterns) {
        UNSCOPED_INFO("  types " << pat << " x" << n);
    }

    CHECK(multiResource > 0);
    CHECK(patterns.size() > 0);
}
