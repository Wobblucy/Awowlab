#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include <cmath>
#include <sstream>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "../../../Core/ErrorLogger.h"

namespace parser {

// Common Parsing Utilities

namespace util {

    // Reads a number from a field, giving 0 for an empty field, "nil", or
    // anything from_chars can't read. Trailing junk after the digits is ignored.
    template <typename T>
    inline T parseNumber(std::string_view sv) {
        T value{};
        if (sv.empty() || sv == "nil") return value;
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
        return value;
    }

    inline int64_t parseInt64(std::string_view sv) { return parseNumber<int64_t>(sv); }
    inline uint64_t parseUint64(std::string_view sv) { return parseNumber<uint64_t>(sv); }
    inline uint32_t parseUint32(std::string_view sv) { return parseNumber<uint32_t>(sv); }
    inline int32_t parseInt32(std::string_view sv) { return parseNumber<int32_t>(sv); }
    inline float parseFloat(std::string_view sv) { return parseNumber<float>(sv); }

    // Resource amounts are written with four decimal places and reach four
    // digits, so float would start shedding precision partway through the range.
    inline double parseDouble(std::string_view sv) { return parseNumber<double>(sv); }

    // Parse bool from string_view (handles "1", "nil", etc.)
    inline bool parseBool(std::string_view sv) {
        return sv == "1";
    }

    // Pull one '|'-separated component out of a field, or an empty view when
    // the field has fewer parts than asked for.
    inline std::string_view powerField(std::string_view sv, size_t index) {
        size_t start = 0;
        for (size_t i = 0; i < index; ++i) {
            size_t bar = sv.find('|', start);
            if (bar == std::string_view::npos) return {};
            start = bar + 1;
        }
        size_t bar = sv.find('|', start);
        return sv.substr(start, bar == std::string_view::npos ? std::string_view::npos
                                                             : bar - start);
    }

    // Advanced logs repeat every resource the spec has across four fields,
    // pipe-separated and index-aligned: "3|4", "85|7", "100|7", "30|7". Older
    // single-resource lines have no bars and simply yield one entry.
    inline void parsePowerEntries(std::string_view types,
                                  std::string_view current,
                                  std::string_view max,
                                  std::string_view cost,
                                  AdvancedUnitInfo& info) {
        info.power_count = 0;
        for (size_t i = 0; i < AdvancedUnitInfo::MAX_POWER_TYPES; ++i) {
            std::string_view type = powerField(types, i);
            if (type.empty()) break;

            auto& entry = info.powers[info.power_count];
            entry.type = static_cast<int8_t>(parseInt32(type));
            entry.current = parseUint32(powerField(current, i));
            entry.max = parseUint32(powerField(max, i));
            entry.cost = parseUint32(powerField(cost, i));
            ++info.power_count;
        }

        // Entry 0 is the primary resource; mirror it into the flat fields so
        // existing callers keep the values they have always read.
        if (info.power_count > 0) {
            info.power_type = info.powers[0].type;
            info.current_power = info.powers[0].current;
            info.max_power = info.powers[0].max;
            info.power_cost = info.powers[0].cost;
        }
    }

    // Fields common to every event. Timestamps are relative to the first
    // event parsed and can be negative for events before it.
    inline void parseBaseEvent(const std::vector<std::string_view>& tokens, BaseEventData& data) {
        data.timestamp_ms = relative_timestamp_ms(EventParser<void>::parse_timestamp_ms(tokens));

        data.source_guid = tokens[TokenIndex::SourceGUID];
        data.source_name = tokens[TokenIndex::SourceName];
        data.source_flags = UnitFlags::parse(tokens[TokenIndex::SourceFlags]);
        data.source_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[TokenIndex::SourceRaidFlags]);

        data.dest_guid = tokens[TokenIndex::DestGUID];
        data.dest_name = tokens[TokenIndex::DestName];
        data.dest_flags = UnitFlags::parse(tokens[TokenIndex::DestFlags]);
        data.dest_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[TokenIndex::DestRaidFlags]);
    }

    // Parse spell prefix data (tokens 11-13)
    inline void parseSpellInfo(const std::vector<std::string_view>& tokens, SpellInfo& spell) {
        spell.spell_id = parseUint32(tokens[TokenIndex::SpellID]);
        spell.spell_name = tokens[TokenIndex::SpellName];
        spell.spell_school = parseUint32(tokens[TokenIndex::SpellSchool]);
    }

    // Facing is stored as radians * 10000 in a uint16_t (see PositionData)
    constexpr float FACING_SCALE = 10000.0f;

    // SWING events have no spell prefix, so their advanced block sits this
    // many tokens earlier than in SPELL events.
    constexpr size_t SWING_OFFSET = 3;

    // Parse the advanced unit block. offset is 0 for SPELL events (tokens
    // 14-32) and SWING_OFFSET for events without a spell prefix.
    inline void parseAdvancedSpellInfoWithOffset(const std::vector<std::string_view>& tokens,
                                                  AdvancedUnitInfo& info,
                                                  size_t offset) {
        using namespace TokenIndex::AdvancedSpell;

        info.info_guid = tokens[InfoGUID - offset];
        info.owner_guid = tokens[OwnerGUID - offset];
        info.current_hp = parseUint64(tokens[CurrentHP - offset]);
        info.max_hp = parseUint64(tokens[MaxHP - offset]);
        info.attack_power = parseUint32(tokens[AttackPower - offset]);
        info.spell_power = parseUint32(tokens[SpellPower - offset]);
        info.armor = parseUint32(tokens[Armor - offset]);
        info.absorb = parseUint32(tokens[Absorb - offset]);
        // Unknown1 and Unknown2 sit between Absorb and the power fields
        parsePowerEntries(tokens[PowerTypeIdx - offset], tokens[CurrentPower - offset],
                          tokens[MaxPower - offset], tokens[PowerCost - offset], info);

        // Raw world coordinates
        info.position.x_loc = parseFloat(tokens[PosX - offset]);
        info.position.y_loc = parseFloat(tokens[PosY - offset]);
        info.position.map_id = static_cast<uint16_t>(parseUint32(tokens[MapID - offset]));
        float facing = parseFloat(tokens[Facing - offset]);
        info.position.facing = static_cast<uint16_t>(std::lround(facing * FACING_SCALE));
        info.level = static_cast<uint8_t>(parseUint32(tokens[Level - offset]));
        info.position.valid = true;
    }

    inline void parseAdvancedSpellInfo(const std::vector<std::string_view>& tokens, AdvancedUnitInfo& info) {
        parseAdvancedSpellInfoWithOffset(tokens, info, 0);
    }

    // Parse extra spell info (for dispels, interrupts, aura breaks, etc.)
    // Parses spell_id, spell_name, spell_school from consecutive tokens starting at index
    inline void parseExtraSpellInfo(const std::vector<std::string_view>& tokens,
                                    size_t start_index,
                                    uint32_t& spell_id,
                                    std::string_view& spell_name,
                                    uint32_t& spell_school) {
        spell_id = parseUint32(tokens[start_index]);
        spell_name = tokens[start_index + 1];
        spell_school = parseUint32(tokens[start_index + 2]);
    }

    // Aura events only distinguish BUFF from DEBUFF; anything else reads as
    // a debuff.
    inline AuraType buffOrDebuff(std::string_view sv) {
        return sv == "BUFF" ? AuraType::BUFF : AuraType::DEBUFF;
    }

    // Damage suffix: amount, baseAmount, overkill, school, resisted, blocked,
    // absorbed, critical, glancing, crushing, then isOffHand on events that
    // carry it. The log's "amount" is after mitigation; baseAmount is the
    // unmitigated figure we report as DamageEventData::amount.
    inline void parseDamageSuffix(const std::vector<std::string_view>& tokens,
                                  size_t start,
                                  bool has_off_hand,
                                  DamageEventData& data) {
        data.mitigated_amount = parseInt64(tokens[start]);
        data.amount = parseInt64(tokens[start + 1]);
        data.overkill = parseInt64(tokens[start + 2]);
        data.damage_school = parseUint32(tokens[start + 3]);
        data.resisted = parseInt64(tokens[start + 4]);
        data.blocked = parseInt64(tokens[start + 5]);
        data.absorbed = parseInt64(tokens[start + 6]);
        data.critical = parseBool(tokens[start + 7]);
        data.glancing = parseBool(tokens[start + 8]);
        data.crushing = parseBool(tokens[start + 9]);
        data.is_off_hand = has_off_hand && parseBool(tokens[start + 10]);
    }

} // namespace util

// SPELL_DAMAGE / SPELL_PERIODIC_DAMAGE Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(11)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Damage suffix starts at token 33

struct SpellDamageTag {};
struct SpellPeriodicDamageTag {};

template <>
struct EventParser<SpellDamageTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};
        PARSER_VALIDATE("SPELL_DAMAGE", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t DAMAGE_SUFFIX_START = 33;
        util::parseDamageSuffix(tokens, DAMAGE_SUFFIX_START, true, data);

        PARSER_FINALIZE("SPELL_DAMAGE", tokens);
        PARSER_DEBUG("SPELL_DAMAGE",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " crit=" << (data.critical ? "yes" : "no")
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// SPELL_PERIODIC_DAMAGE uses same structure as SPELL_DAMAGE
template <>
struct EventParser<SpellPeriodicDamageTag> : public EventParser<SpellDamageTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellDamageTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_DAMAGE", tokens);
        return data;
    }
};

// SWING_DAMAGE_LANDED Parser
// Token count: 40
// Structure: base(11) + advanced(19) + damage_suffix(10)
// There is no spell prefix, so every index after the base sits 3 lower than
// in SPELL_DAMAGE, and the suffix has no isOffHand. The spell fields are set
// to the synthetic melee entry (1, "Melee", physical).

struct SwingDamageLandedTag {};

template <>
struct EventParser<SwingDamageLandedTag> {
    static constexpr size_t expected_token_count() { return 40; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};
        PARSER_VALIDATE("SWING_DAMAGE_LANDED", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        data.spell.spell_id = 1;
        data.spell.spell_name = "Melee";
        data.spell.spell_school = SpellSchool::Physical;

        util::parseAdvancedSpellInfoWithOffset(tokens, data.advanced_info, util::SWING_OFFSET);

        constexpr size_t DAMAGE_SUFFIX_START = 30;
        util::parseDamageSuffix(tokens, DAMAGE_SUFFIX_START, false, data);

        PARSER_FINALIZE("SWING_DAMAGE_LANDED", tokens);
        PARSER_DEBUG("SWING_DAMAGE_LANDED",
            "amount=" << data.amount
            << " crit=" << (data.critical ? "yes" : "no")
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// DAMAGE_SPLIT Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(11)
// Same structure as SPELL_DAMAGE

struct DamageSplitTag {};

template <>
struct EventParser<DamageSplitTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};
        PARSER_VALIDATE("DAMAGE_SPLIT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);

        util::parseSpellInfo(tokens, data.spell);

        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t DAMAGE_SUFFIX_START = 33;
        util::parseDamageSuffix(tokens, DAMAGE_SUFFIX_START, true, data);

        PARSER_FINALIZE("DAMAGE_SPLIT", tokens);
        PARSER_DEBUG("DAMAGE_SPLIT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount);

        return data;
    }
};

} // namespace parser
