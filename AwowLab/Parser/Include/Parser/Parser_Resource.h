#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include <cmath>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "Parser_Damage.h"  // For util namespace

namespace parser {

namespace util {
    // Energize, drain and leech share one suffix: amount, overEnergize,
    // powerType, altPowerType. The amounts are written with four decimal
    // places (and can be negative for drains), so they must be read as
    // floating point - an integer read stops at the '.' and floors every
    // gain. altPowerType is usually nil and not kept.
    inline void parseEnergizeSuffix(const std::vector<std::string_view>& tokens,
                                    size_t start,
                                    EnergizeEventData& data) {
        data.amount = parseDouble(tokens[start]);
        data.over_energize = parseDouble(tokens[start + 1]);
        data.power_type = static_cast<PowerType>(parseInt32(tokens[start + 2]));
    }
}

// SPELL_ENERGIZE Parser
// Token count: 37
// Structure: base(11) + spell(3) + advanced(19) + energize_suffix(4)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Energize suffix starts at token 33: amount, overEnergize, powerType, altPowerType
// Reference: https://warcraft.wiki.gg/wiki/COMBAT_LOG_EVENT

struct SpellEnergizeTag {};

template <>
struct EventParser<SpellEnergizeTag> {
    static constexpr size_t expected_token_count() { return 37; }

    static EnergizeEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        EnergizeEventData data{};
        PARSER_VALIDATE("SPELL_ENERGIZE", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t ENERGIZE_SUFFIX_START = 33;
        util::parseEnergizeSuffix(tokens, ENERGIZE_SUFFIX_START, data);

        PARSER_FINALIZE("SPELL_ENERGIZE", tokens);
        PARSER_DEBUG("SPELL_ENERGIZE",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " amount=" << data.amount
            << " powerType=" << static_cast<int>(data.power_type)
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// SPELL_PERIODIC_ENERGIZE Parser
// Token count: 37 (same structure as SPELL_ENERGIZE)

struct SpellPeriodicEnergizeTag {};

template <>
struct EventParser<SpellPeriodicEnergizeTag> : public EventParser<SpellEnergizeTag> {
    static constexpr size_t expected_token_count() { return 37; }

    static EnergizeEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellEnergizeTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_ENERGIZE", tokens);
        return data;
    }
};

// SPELL_DRAIN Parser
// Token count: 37 (same as energize)
// Structure: base(11) + spell(3) + advanced(19) + drain_suffix(4)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Reference: https://warcraft.wiki.gg/wiki/COMBAT_LOG_EVENT

struct SpellDrainTag {};

template <>
struct EventParser<SpellDrainTag> {
    static constexpr size_t expected_token_count() { return 37; }

    static EnergizeEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        EnergizeEventData data{};
        PARSER_VALIDATE("SPELL_DRAIN", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t DRAIN_SUFFIX_START = 33;
        util::parseEnergizeSuffix(tokens, DRAIN_SUFFIX_START, data);

        PARSER_FINALIZE("SPELL_DRAIN", tokens);
        PARSER_DEBUG("SPELL_DRAIN",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " powerType=" << static_cast<int>(data.power_type));

        return data;
    }
};

// SPELL_LEECH Parser
// Token count: 37 (same as energize)
// Structure: base(11) + spell(3) + advanced(19) + leech_suffix(4)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Reference: https://warcraft.wiki.gg/wiki/COMBAT_LOG_EVENT

struct SpellLeechTag {};

template <>
struct EventParser<SpellLeechTag> {
    static constexpr size_t expected_token_count() { return 37; }

    static EnergizeEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        EnergizeEventData data{};
        PARSER_VALIDATE("SPELL_LEECH", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        // No leech lines turned up in the logs on hand to confirm the
        // decimals, but the fields are doubles and an integer read could only
        // lose data.
        constexpr size_t LEECH_SUFFIX_START = 33;
        util::parseEnergizeSuffix(tokens, LEECH_SUFFIX_START, data);

        PARSER_FINALIZE("SPELL_LEECH", tokens);
        PARSER_DEBUG("SPELL_LEECH",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " powerType=" << static_cast<int>(data.power_type));

        return data;
    }
};

} // namespace parser
