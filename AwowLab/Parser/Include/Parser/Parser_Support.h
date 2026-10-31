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
#include "Parser_Heal.h"    // For util::parseHealSuffix

namespace parser {

// Support Event Data Structure
// Support events have an extra supporterGUID at the end identifying the
// augmentation evoker or other support source

struct SupportEventData : public DamageEventData {
    std::string_view supporter_guid;  // GUID of the support source (e.g., Aug Evoker)
};

struct HealSupportEventData : public HealEventData {
    std::string_view supporter_guid;
};

// SPELL_DAMAGE_SUPPORT Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(10) + supporterGUID(1)
// Support events have 10 damage suffix fields (no isOffHand), not 11
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)

struct SpellDamageSupportTag {};

template <>
struct EventParser<SpellDamageSupportTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static SupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SupportEventData data{};
        PARSER_VALIDATE("SPELL_DAMAGE_SUPPORT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t DAMAGE_SUFFIX_START = 33;
        util::parseDamageSuffix(tokens, DAMAGE_SUFFIX_START, false, data);

        data.supporter_guid = tokens.back();

        PARSER_FINALIZE("SPELL_DAMAGE_SUPPORT", tokens);
        PARSER_DEBUG("SPELL_DAMAGE_SUPPORT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " supporter=" << data.supporter_guid
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// SPELL_PERIODIC_DAMAGE_SUPPORT Parser
// Token count: 44 (same structure as SPELL_DAMAGE_SUPPORT)

struct SpellPeriodicDamageSupportTag {};

template <>
struct EventParser<SpellPeriodicDamageSupportTag> : public EventParser<SpellDamageSupportTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static SupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellDamageSupportTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_DAMAGE_SUPPORT", tokens);
        return data;
    }
};

// SPELL_HEAL_SUPPORT Parser
// Token count: 39
// Structure: base(11) + spell(3) + advanced(19) + heal_suffix(5) + supporterGUID(1)
// heal_suffix: amount, baseAmount, overhealing, absorbed, critical
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)

struct SpellHealSupportTag {};

template <>
struct EventParser<SpellHealSupportTag> {
    static constexpr size_t expected_token_count() { return 39; }

    static HealSupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        HealSupportEventData data{};
        PARSER_VALIDATE("SPELL_HEAL_SUPPORT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t HEAL_SUFFIX_START = 33;
        util::parseHealSuffix(tokens, HEAL_SUFFIX_START, data);

        data.supporter_guid = tokens.back();

        PARSER_FINALIZE("SPELL_HEAL_SUPPORT", tokens);
        PARSER_DEBUG("SPELL_HEAL_SUPPORT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " supporter=" << data.supporter_guid
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// SPELL_PERIODIC_HEAL_SUPPORT Parser
// Token count: 39 (same structure as SPELL_HEAL_SUPPORT)

struct SpellPeriodicHealSupportTag {};

template <>
struct EventParser<SpellPeriodicHealSupportTag> : public EventParser<SpellHealSupportTag> {
    static HealSupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellHealSupportTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_HEAL_SUPPORT", tokens);
        return data;
    }
};

// SWING_DAMAGE_LANDED_SUPPORT Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(10) + supporterGUID(1)
// Unlike plain SWING_DAMAGE_LANDED, the support variant carries a spell
// prefix holding the supporter's aura (e.g. "395152, Ebon Might, 0xc"), so
// the layout matches SPELL_DAMAGE_SUPPORT.

struct SwingDamageLandedSupportTag {};

template <>
struct EventParser<SwingDamageLandedSupportTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static SupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SupportEventData data{};
        PARSER_VALIDATE("SWING_DAMAGE_LANDED_SUPPORT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);
        util::parseSpellInfo(tokens, data.spell);
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t DAMAGE_SUFFIX_START = 33;
        util::parseDamageSuffix(tokens, DAMAGE_SUFFIX_START, false, data);

        data.supporter_guid = tokens.back();

        PARSER_FINALIZE("SWING_DAMAGE_LANDED_SUPPORT", tokens);
        PARSER_DEBUG("SWING_DAMAGE_LANDED_SUPPORT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " supporter=" << data.supporter_guid);

        return data;
    }
};

} // namespace parser
