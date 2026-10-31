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

// UNIT_DIED Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a unit dies

struct UnitDiedTag {};

template <>
struct EventParser<UnitDiedTag> {
    static constexpr size_t expected_token_count() { return 12; }

    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DeathEventData data{};

        PARSER_VALIDATE("UNIT_DIED", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        data.recap_id = util::parseInt32(tokens[11]);

        data.unconscious_on_death = false;
        data.has_position = false;

        PARSER_FINALIZE("UNIT_DIED", tokens);
        PARSER_DEBUG("UNIT_DIED",
            "unit=" << data.dest_name << " (" << data.dest_guid << ")"
            << " recapID=" << data.recap_id);

        return data;
    }
};

// UNIT_DESTROYED Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a unit is destroyed (totems, objects, etc.)

struct UnitDestroyedTag {};

template <>
struct EventParser<UnitDestroyedTag> : public EventParser<UnitDiedTag> {
    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<UnitDiedTag>::parse_and_return(tokens);
        PARSER_FINALIZE("UNIT_DESTROYED", tokens);
        return data;
    }
};

// UNIT_DISSIPATES Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a unit dissipates (fades away)

struct UnitDissipatesTag {};

template <>
struct EventParser<UnitDissipatesTag> : public EventParser<UnitDiedTag> {
    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<UnitDiedTag>::parse_and_return(tokens);
        PARSER_FINALIZE("UNIT_DISSIPATES", tokens);
        return data;
    }
};

// PARTY_KILL Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a party member kills something

struct PartyKillTag {};

template <>
struct EventParser<PartyKillTag> {
    static constexpr size_t expected_token_count() { return 12; }

    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DeathEventData data{};

        PARSER_VALIDATE("PARTY_KILL", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        data.recap_id = util::parseInt32(tokens[11]);

        data.has_position = false;

        PARSER_FINALIZE("PARTY_KILL", tokens);
        PARSER_DEBUG("PARTY_KILL",
            "killer=" << data.source_name << " killed=" << data.dest_name
            << " recapID=" << data.recap_id);

        return data;
    }
};

// SPELL_SUMMON Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a unit summons something (pets, totems, etc.)

struct SpellSummonTag {};

template <>
struct EventParser<SpellSummonTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static SummonEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SummonEventData data{};

        PARSER_VALIDATE("SPELL_SUMMON", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        data.has_position = false;

        PARSER_FINALIZE("SPELL_SUMMON", tokens);
        PARSER_DEBUG("SPELL_SUMMON",
            "summoner=" << data.source_name << " summoned=" << data.dest_name
            << " spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

// SPELL_CREATE Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a spell creates something (conjured items, etc.)

struct SpellCreateTag {};

template <>
struct EventParser<SpellCreateTag> : public EventParser<SpellSummonTag> {
    static SummonEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellSummonTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_CREATE", tokens);
        return data;
    }
};

// SPELL_RESURRECT Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a unit is resurrected

struct SpellResurrectTag {};

template <>
struct EventParser<SpellResurrectTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static SummonEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SummonEventData data{};

        PARSER_VALIDATE("SPELL_RESURRECT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        data.has_position = false;

        PARSER_FINALIZE("SPELL_RESURRECT", tokens);
        PARSER_DEBUG("SPELL_RESURRECT",
            "resurrector=" << data.source_name << " resurrected=" << data.dest_name
            << " spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

// SPELL_INSTAKILL Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a spell instantly kills a unit

struct SpellInstakillTag {};

template <>
struct EventParser<SpellInstakillTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DeathEventData data{};

        PARSER_VALIDATE("SPELL_INSTAKILL", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        data.spell_id = util::parseUint32(tokens[TokenIndex::SpellID]);
        data.spell_name = tokens[TokenIndex::SpellName];

        data.has_position = false;

        PARSER_FINALIZE("SPELL_INSTAKILL", tokens);
        PARSER_DEBUG("SPELL_INSTAKILL",
            "spell=" << data.spell_id << " (" << data.spell_name << ")"
            << " killed=" << data.dest_name);

        return data;
    }
};

// ENVIRONMENTAL_DAMAGE Parser
// Token count: 41 with advanced logging, shorter without
// Structure (advanced): base(11) + advanced(19, as SWING) + environmentalType(1)
//                       + damage suffix(10, as SWING)
// Structure (plain):    base(11) + environmentalType(1) + amount + ...
// Triggered by environmental damage (fire, falling, drowning, etc.)

struct EnvironmentalDamageTag {};

template <>
struct EventParser<EnvironmentalDamageTag> {
    static constexpr size_t minimum_token_count() { return 13; }
    static constexpr size_t advanced_token_count() { return 41; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};

        PARSER_VALIDATE("ENVIRONMENTAL_DAMAGE", tokens, minimum_token_count(), data);

        util::parseBaseEvent(tokens, data);

        // With advanced logging the unit block sits where SWING has it
        // (11-29), pushing the type to 30 and the damage suffix to 31.
        const bool advanced = tokens.size() >= advanced_token_count();
        if (advanced) {
            util::parseAdvancedSpellInfoWithOffset(tokens, data.advanced_info, util::SWING_OFFSET);
        }

        // Environmental type at index 11, or 30 with advanced logging
        std::string_view envType = tokens[advanced ? 30 : 11];
        data.spell.spell_id = 0;
        data.spell.spell_name = envType;  // Use env type as "spell name"

        // Drowning, Fatigue, Falling and anything unrecognised count as physical
        if (envType == "Fire" || envType == "Lava") {
            data.spell.spell_school = SpellSchool::Fire;
        } else if (envType == "Slime") {
            data.spell.spell_school = SpellSchool::Nature;
        } else {
            data.spell.spell_school = SpellSchool::Physical;
        }

        if (advanced) {
            // Same suffix as SWING_DAMAGE_LANDED, with no isOffHand
            constexpr size_t DAMAGE_SUFFIX_START = 31;
            util::parseDamageSuffix(tokens, DAMAGE_SUFFIX_START, false, data);
        } else {
            data.amount = util::parseInt64(tokens[12]);

            // Additional damage fields may follow
            if (tokens.size() > 13) {
                data.overkill = util::parseInt64(tokens[13]);
            }
            if (tokens.size() > 14) {
                data.damage_school = util::parseUint32(tokens[14]);
            }
            if (tokens.size() > 15) {
                data.resisted = util::parseInt64(tokens[15]);
            }
            if (tokens.size() > 16) {
                data.blocked = util::parseInt64(tokens[16]);
            }
            if (tokens.size() > 17) {
                data.absorbed = util::parseInt64(tokens[17]);
            }
            if (tokens.size() > 18) {
                data.critical = util::parseBool(tokens[18]);
            }

            data.advanced_info.position.valid = false;
        }

        PARSER_FINALIZE("ENVIRONMENTAL_DAMAGE", tokens);
        PARSER_DEBUG("ENVIRONMENTAL_DAMAGE",
            "type=" << envType << " amount=" << data.amount
            << " target=" << data.dest_name);

        return data;
    }
};

// EMOTE Parser
// Token count: 11 (just base event)
// Structure: base(11)
// Triggered for combat-related emotes

struct EmoteTag {};

template <>
struct EventParser<EmoteTag> {
    static constexpr size_t expected_token_count() { return 11; }

    static BaseEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        BaseEventData data{};

        PARSER_VALIDATE("EMOTE", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        PARSER_FINALIZE("EMOTE", tokens);
        PARSER_DEBUG("EMOTE",
            "source=" << data.source_name << " dest=" << data.dest_name);

        return data;
    }
};

} // namespace parser
