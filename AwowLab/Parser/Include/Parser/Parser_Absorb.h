#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "Parser_Damage.h"  // For util namespace

namespace parser {

namespace util {
    // Reads everything after the base fields: the absorbed spell (spell
    // absorbs only), the absorber, the absorb spell and the amount. Lines at
    // least spell_absorb_token_count long are spell absorbs, which carry the
    // absorbed spell at 11-13; a melee absorb starts the absorber at 11.
    // Returns the index of the amount token.
    inline size_t parseAbsorberAndAmount(const std::vector<std::string_view>& tokens,
                                         size_t spell_absorb_token_count,
                                         AbsorbedEventData& data) {
        size_t absorber_start;
        if (tokens.size() >= spell_absorb_token_count) {
            parseSpellInfo(tokens, data.spell);
            absorber_start = 14;
        } else {
            data.spell.spell_id = 0;
            data.spell.spell_name = "Melee";
            data.spell.spell_school = SpellSchool::Physical;
            absorber_start = 11;
        }

        data.absorber_guid = tokens[absorber_start];
        data.absorber_name = tokens[absorber_start + 1];
        data.absorber_flags = UnitFlags::parse(tokens[absorber_start + 2]);
        data.absorber_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[absorber_start + 3]);

        const size_t absorb_spell_start = absorber_start + 4;
        data.absorb_spell_id = parseUint32(tokens[absorb_spell_start]);
        data.absorb_spell_name = tokens[absorb_spell_start + 1];
        data.absorb_spell_school = parseUint32(tokens[absorb_spell_start + 2]);

        const size_t amount_index = absorb_spell_start + 3;
        data.amount = parseInt64(tokens[amount_index]);
        return amount_index;
    }
}

// SPELL_ABSORBED Parser
// Token count: 22-23
// Structure varies based on whether the absorb was from a spell or swing:
//
// SPELL absorb (22 tokens):
// base(11) + absorbed_spell(3) + absorber(4) + absorb_spell(3) + amount(1)
//
// SWING absorb (19 tokens):
// base(11) + absorber(4) + absorb_spell(3) + amount(1)
//
// absorber(4) = absorber_guid, absorber_name, absorber_flags, absorber_raid_flags

struct SpellAbsorbedTag {};

template <>
struct EventParser<SpellAbsorbedTag> {
    static constexpr size_t minimum_token_count() { return 19; }

    static AbsorbedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AbsorbedEventData data{};
        PARSER_VALIDATE("SPELL_ABSORBED", tokens, minimum_token_count(), data);

        util::parseBaseEvent(tokens, data);

        constexpr size_t SPELL_ABSORB_TOKEN_COUNT = 22;
        size_t amount_index = util::parseAbsorberAndAmount(tokens, SPELL_ABSORB_TOKEN_COUNT, data);

        // Critical flag may follow the amount
        if (tokens.size() > amount_index + 1) {
            data.critical = util::parseBool(tokens[amount_index + 1]);
        }

        data.has_position = false;

        PARSER_FINALIZE("SPELL_ABSORBED", tokens);
        PARSER_DEBUG("SPELL_ABSORBED",
            "absorbed_spell=" << data.spell.spell_id
            << " absorber=" << data.absorber_name
            << " absorb_spell=" << data.absorb_spell_id << " (" << data.absorb_spell_name << ")"
            << " amount=" << data.amount);

        return data;
    }
};

// SPELL_ABSORBED_SUPPORT Parser
// Token count: 23-24
// Same as SPELL_ABSORBED but with supporterGUID at the end

struct SpellAbsorbedSupportTag {};

template <>
struct EventParser<SpellAbsorbedSupportTag> {
    static constexpr size_t minimum_token_count() { return 20; }

    static AbsorbedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AbsorbedEventData data{};
        PARSER_VALIDATE("SPELL_ABSORBED_SUPPORT", tokens, minimum_token_count(), data);

        util::parseBaseEvent(tokens, data);

        // One token longer than SPELL_ABSORBED for the trailing supporter GUID
        constexpr size_t SPELL_ABSORB_TOKEN_COUNT = 23;
        util::parseAbsorberAndAmount(tokens, SPELL_ABSORB_TOKEN_COUNT, data);

        data.supporter_guid = tokens.back();

        data.has_position = false;

        PARSER_FINALIZE("SPELL_ABSORBED_SUPPORT", tokens);
        PARSER_DEBUG("SPELL_ABSORBED_SUPPORT",
            "absorbed_spell=" << data.spell.spell_id
            << " absorb_spell=" << data.absorb_spell_id
            << " amount=" << data.amount
            << " supporter=" << data.supporter_guid);

        return data;
    }
};

} // namespace parser
