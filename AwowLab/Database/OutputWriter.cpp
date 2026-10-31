#include "OutputWriter.h"
#include "EmoteText.h"
#include "OutputWriter/EventExtractorContext.h"
#include "OutputWriter/Extractors/DeathResurrectExtractor.h"
#include "OutputWriter/Extractors/AuraExtractor.h"
#include "OutputWriter/Extractors/DispelInterruptExtractor.h"
#include "Core/StringInterner.h"
// Parser headers
#include "Parser/Parser_Damage.h"
#include "Parser/Parser_Cast.h"
#include "Parser/Parser_Heal.h"
#include "Parser/Parser_Missed.h"
#include "Parser/Parser_Absorb.h"
#include "Parser/Parser_Resource.h"
#include "Parser/Parser_Support.h"
#include "Parser/Parser_Meta.h"
#include "Parser/Parser_Combatant_Info.h"
#include "Parser/Parser_Encounter.h"
#include "Parser/Parser_ChallengeMode.h"
#include "Parser/SpecializationColors.h"
#include "Parser/ParserDebug.h"
#include "Parser/Parser.h"
#include "CooldownModifiers/CooldownModifierRegistry.h"
#include "CooldownModifiers/CooldownModifierTypes.h"
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

// Use extraction helpers
using namespace awow::extract;
using awow::getActorTypeFromGuid;

// Emote text cleaning lives in EmoteText.h so the live overlay session
// produces byte-identical emote strings (phase rules match on them).

ExtractedData OutputWriter::extractAllEvents(
    const std::vector<std::vector<std::string_view>>& all_lines
) {
    ExtractedData result;

    // Pre-scan to find CHALLENGE_MODE_START or ENCOUNTER_START timestamp
    // This becomes our 0:00 reference point - events before it will have negative timestamps
    // CHALLENGE_MODE_START takes priority for M+ dungeons (it appears before boss encounters)
    uint64_t encounterStartTimestamp = 0;
    for (const auto& tokens : all_lines) {
        if (tokens.size() < 3) continue;
        std::string_view eventType = tokens[2];

        // Check CHALLENGE_MODE_START first - M+ dungeon start takes priority
        if (eventType == "CHALLENGE_MODE_START" &&
            tokens.size() >= parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {
            encounterStartTimestamp = parser::EventParser<void>::parse_timestamp_ms(tokens);
            break;  // M+ start found - use this as 0:00
        }
        // Fall back to ENCOUNTER_START for raid boss encounters
        if (eventType == "ENCOUNTER_START" &&
            tokens.size() >= parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {
            encounterStartTimestamp = parser::EventParser<void>::parse_timestamp_ms(tokens);
            break;  // Boss encounter start found
        }
    }

    // Set the log start timestamp to the encounter start (0:00 = encounter begins)
    // If no encounter marker found, fall back to first event setting it (legacy behavior)
    parser::get_log_start_timestamp_ms() = encounterStartTimestamp;

    // Pre-allocate vectors based on line count estimates to avoid reallocations
    // These estimates are based on typical combat log composition:
    // ~33% have position data, ~50% are combat events, ~25% are spell casts
    size_t lineCount = all_lines.size();
    result.actorEvents.reserve(lineCount / 3);
    result.combatEvents.reserve(lineCount / 2);
    result.spellCastEvents.reserve(lineCount / 4);
    result.auraEvents.reserve(lineCount / 5);
    result.resourceEvents.reserve(lineCount / 3);
    result.deathEvents.reserve(100);        // Deaths are relatively rare
    result.resurrectEvents.reserve(50);     // Resurrects are even rarer
    result.mapChangeEvents.reserve(10);     // Usually just a few map changes
    result.combatantInfoEvents.reserve(40); // Up to 40 players in raid

    // Caches - built during extraction
    std::unordered_map<std::string, uint16_t> specIdCache;     // Player GUID -> SpecID (for COMBATANT_INFO tracking)
    std::unordered_map<std::string, EventExtractorContext::PendingCast> pendingCastStarts;
    haste::HasteTracker hasteTracker;

    // Resource-based cooldown reduction tracking
    // Map: player GUID -> power_type -> vector of modifiers for that power type
    struct ActorResourceModifiers {
        std::unordered_map<int8_t, std::vector<awow::CooldownModifier>> modifiers_by_power_type;
    };
    std::unordered_map<std::string, ActorResourceModifiers> actorResourceModifiers;

    // Helper to register spell names (for UI display in death analysis, etc.)
    auto registerSpellName = [&result](uint32_t spell_id, std::string_view spell_name) {
        if (spell_id > 0 && !spell_name.empty()) {
            result.spellNameMap.try_emplace(spell_id, std::string(spell_name));
        }
    };

    // Helper to register GUID -> Name mappings (for resolving pet/guardian names)
    auto registerGuidName = [&result](std::string_view guid, std::string_view name) {
        if (!guid.empty() && !name.empty()) {
            result.guidToNameMap.try_emplace(std::string(guid), std::string(name));
        }
    };

    // Helper to track first-seen timestamps (for AuraDatabase removal-only buffs)
    // Only needed for actors with position data - can't annotate actors we can't render
    auto registerFirstSeen = [&result](std::string_view guid, int32_t timestamp_ms) {
        if (guid.empty()) return;
        std::string key(guid);
        auto [it, inserted] = result.firstSeenTimestamps.try_emplace(key, timestamp_ms);
        if (!inserted && timestamp_ms < it->second) {
            it->second = timestamp_ms;
        }
    };

    // Deduct the Aug support slice from the amplified actor's parent
    // record so downstream aggregation reads their damage/healing net of
    // what the supporter added. Walks backward from the tail of
    // result.combatEvents looking for the parent that spawned this
    // shadow event, using the same match rules verified against real
    // logs: same source_guid, same target_guid, same spell_school,
    // SWING <-> SWING (or both non-SWING), timestamp within +/-2ms.
    // Support-flagged records are skipped so a shadow never matches
    // another shadow. On no match we still credit the supporter (small
    // drift accepted, matches the live overlay behaviour).
    auto deductSupportFromParent = [&result](
        uint32_t amplified_source_guid_id,
        uint32_t target_guid_id,
        int32_t timestamp_ms,
        uint8_t spell_school,
        bool is_swing,
        int64_t support_amount,
        int64_t support_effective,
        CombatEventType event_type) {
        if (amplified_source_guid_id == 0) return;
        for (auto rit = result.combatEvents.rbegin();
             rit != result.combatEvents.rend();
             ++rit) {
            if (timestamp_ms - rit->timestamp_ms > 2) break;
            if (rit->event_type != event_type) continue;
            if (rit->flags & CombatEventFlags::Support) continue;
            if (rit->source_guid_id != amplified_source_guid_id) continue;
            if (rit->target_guid_id != target_guid_id) continue;
            if (rit->spell_school != spell_school) continue;
            const bool parent_is_swing =
                (rit->flags & CombatEventFlags::IsSwing) != 0;
            if (parent_is_swing != is_swing) continue;
            rit->amount -= support_amount;
            rit->effective_amount -= support_effective;
            return;
        }
    };

    // A support record is credited to the supporter; its amounts are taken
    // back off the amplified caster's parent record.
    auto recordSupport = [&result, &deductSupportFromParent](
        CombatEvent combatEvent, std::string_view amplified_source_guid, bool is_swing) {
        deductSupportFromParent(
            guidInterner().intern(amplified_source_guid),
            combatEvent.target_guid_id,
            combatEvent.timestamp_ms,
            combatEvent.spell_school,
            is_swing,
            combatEvent.amount,
            combatEvent.effective_amount,
            combatEvent.event_type);
        result.combatEvents.push_back(std::move(combatEvent));
    };

    auto recordDamage = [&result](const auto& data, uint8_t school, uint16_t flags) {
        auto combatEvent = createDamageCombatEvent(
            data.source_guid, data.dest_guid, data.advanced_info.owner_guid,
            data.timestamp_ms, data.spell.spell_id, data.amount, data.mitigated_amount,
            data.overkill, data.absorbed, data.blocked, data.resisted,
            school, data.critical);
        combatEvent.flags = flags;

        // Friendly fire (self-damage, Spirit Link, etc.) stays a damage
        // record so it still counts as damage taken by the target;
        // damage-done queries skip the flag. Same accounting as the Details!
        // addon. Both sides must be friendly - a target-only check would
        // also catch every boss hit on a player.
        if (data.source_flags.isFriendly() && data.dest_flags.isFriendly()) {
            combatEvent.flags |= CombatEventFlags::FriendlyFire;
        }
        result.combatEvents.push_back(std::move(combatEvent));
    };

    // A cast that started but ended without succeeding
    auto recordCancelledCast = [&result, &hasteTracker](
        const std::string& actorGuid, const EventExtractorContext::PendingCast& pending, int32_t endMs) {
        SpellCastEvent cancelledCast;
        cancelledCast.actor_guid = actorGuid;
        cancelledCast.spell_id = pending.spell_id;
        cancelledCast.start_timestamp_ms = pending.start_timestamp_ms;
        cancelledCast.end_timestamp_ms = endMs;
        cancelledCast.haste_permille = haste::HasteTracker::permilleToTenthsPercent(
            hasteTracker.getHasteAtTime(actorGuid, pending.start_timestamp_ms));
        cancelledCast.cancelled = true;
        result.spellCastEvents.push_back(std::move(cancelledCast));
    };

    // Process each tokenized line in a single pass
    for (const auto& tokens : all_lines) {
        if (tokens.size() < 3) {
            continue;
        }

        // Check event type (token[2]) and use appropriate parser
        std::string_view eventType = tokens[2];

        // Handle COMBATANT_INFO separately (different structure, no position data)
        // These events come first, so we build the specIdCache before processing position events
        if (eventType == "COMBATANT_INFO") {
            if (tokens.size() >= parser::EventParser<parser::CombatantInfoTag>::minimum_token_count()) {
                auto data = parser::EventParser<parser::CombatantInfoTag>::parse_and_return(tokens);

                if (!data.player_guid.empty() && data.spec_id > 0) {
                    CombatantInfoEvent info;
                    info.player_guid = std::string(data.player_guid);
                    info.spec_id = data.spec_id;
                    info.talents = std::move(data.talents);  // Move parsed talents

                    // Cache spec ID for later use (color will be assigned by ActorColorGenerator)
                    specIdCache[info.player_guid] = info.spec_id;

                    // Initialize haste tracking with the highest haste value
                    // (usually hasteSpell and hasteRanged are the same)
                    int32_t hasteRating = std::max({data.haste_melee, data.haste_ranged, data.haste_spell});
                    hasteTracker.setBaseHasteFromRating(info.player_guid, hasteRating);

                    // Build resource-based cooldown modifier lookup
                    // Check each talent to see if it grants resource-based CD reductions
                    auto& registry = awow::CooldownModifierRegistry::instance();
                    for (const auto& talent : info.talents) {
                        const auto* modifiers = registry.getModifiersForTalent(talent.spell_id);
                        if (modifiers) {
                            for (const auto& modifier : *modifiers) {
                                if (modifier.modifier_type == awow::ModifierType::ResourceBased) {
                                    // Group by power_type for fast lookup during SPELL_CAST_SUCCESS
                                    int8_t powerType = static_cast<int8_t>(modifier.resource.power_type);
                                    actorResourceModifiers[info.player_guid].modifiers_by_power_type[powerType].push_back(modifier);
                                }
                            }
                        }
                    }

                    result.combatantInfoEvents.push_back(std::move(info));
                }
            }
            continue;
        }

        // Handle MAP_CHANGE events (provides map boundaries for floor texture rendering)
        if (eventType == "MAP_CHANGE") {
            if (tokens.size() >= parser::EventParser<parser::MapChangeTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::MapChangeTag>::parse_and_return(tokens);

                MapChangeEvent mapEvent;
                mapEvent.timestamp_ms = static_cast<uint32_t>(
                    parser::EventParser<void>::parse_timestamp_ms(tokens) - parser::get_log_start_timestamp_ms()
                );
                mapEvent.map_id = data.map_id;
                mapEvent.map_name = std::string(data.map_name);
                mapEvent.x1 = data.x1;
                mapEvent.y1 = data.y1;
                mapEvent.x2 = data.x2;
                mapEvent.y2 = data.y2;

                result.mapChangeEvents.push_back(std::move(mapEvent));
            }
            continue;
        }

        // Deaths, auras and dispels carry no position data, so they are handled first
        if (extractDeathResurrectEvent(eventType, tokens, result, registerGuidName)) {
            continue;
        }

        {
            AuraExtractorContext auraCtx{result, hasteTracker, specIdCache, registerGuidName};
            if (extractAuraEvent(eventType, tokens, auraCtx)) {
                continue;
            }
        }

        if (extractDispelInterruptEvent(eventType, tokens, result, registerSpellName, registerGuidName)) {
            continue;
        }

        // EMOTE lines skip the usual flag fields:
        //   EMOTE,srcGuid,"SrcName",destGuid,"DestName",<free text>
        // The text is unquoted, so the tokenizer splits it on spaces and
        // commas - everything from token 7 on is the emote body. Phase
        // rules can split a boss fight at the first matching emote.
        if (eventType == "EMOTE") {
            if (tokens.size() >= 8) {
                EmoteEvent emote;
                emote.timestamp_ms = parser::relative_timestamp_ms(
                    parser::EventParser<void>::parse_timestamp_ms(tokens));
                emote.source_guid = std::string(tokens[3]);
                emote.source_name = std::string(tokens[4]);
                registerGuidName(tokens[3], tokens[4]);

                emote.text = awow::emote::emoteTextFromTokens(tokens);

                if (!emote.text.empty()) {
                    result.emoteEvents.push_back(std::move(emote));
                }
            }
            continue;
        }

        // Skip lines that are too short for position events
        if (tokens.size() < 14) {
            continue;
        }
        UnitRendering ur{};
        bool parsed = false;
        bool isSpellCastSuccess = false;
        uint32_t spell_id = 0;
        std::string spell_name;
        ParsedEventCommon common{};

        // Capture registerGuidName for use in populateCommon template
        auto doPopulateCommon = [&common, &registerGuidName, &result](const auto& data) {
            populateCommon(common, data, registerGuidName);

            // Every event names two units and carries the flags for both, so
            // this sees enemies that never report a position - a mob the raid
            // burns down without it acting produces no actor or resource
            // event, and used to go missing from anything keyed on hostility.
            result.noteActorReaction(common.source_guid, common.source_flags);
            result.noteActorReaction(common.dest_guid, common.dest_flags);
        };

        // Events with an advanced block carry one unit's position
        auto notePositionedEvent = [&](const auto& data) {
            ur.timestamp_ms = data.timestamp_ms;
            extractPosition(ur, data.advanced_info);
            parsed = data.advanced_info.position.valid;
            doPopulateCommon(data);
        };

        if (eventType == "SPELL_DAMAGE" || eventType == "RANGE_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                const uint16_t flags = buildDamageFlags(
                    data.critical, data.overkill, data.absorbed, data.blocked, data.resisted,
                    data.glancing, data.crushing, data.is_off_hand);
                recordDamage(data, static_cast<uint8_t>(data.damage_school), flags);
            }
        } else if (eventType == "SPELL_PERIODIC_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicDamageTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                const uint16_t flags = buildDamageFlags(
                    data.critical, data.overkill, data.absorbed, data.blocked, data.resisted,
                    false, false, false, true);  // is_periodic = true
                recordDamage(data, static_cast<uint8_t>(data.damage_school), flags);
            }
        } else if (eventType == "SWING_DAMAGE_LANDED") {
            if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingDamageLandedTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                const uint16_t flags = buildDamageFlags(
                    data.critical, data.overkill, data.absorbed, data.blocked, data.resisted,
                    data.glancing, data.crushing, data.is_off_hand, false, true);  // is_swing = true
                recordDamage(data, static_cast<uint8_t>(data.spell.spell_school), flags);
            }
        } else if (eventType == "DAMAGE_SPLIT") {
            if (tokens.size() >= parser::EventParser<parser::DamageSplitTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::DamageSplitTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                const uint16_t flags = buildDamageFlags(
                    data.critical, 0, data.absorbed, 0, 0, false, false, false);
                recordDamage(data, static_cast<uint8_t>(data.damage_school), flags);
            }
        }
        // Store all miss types for debug dumping, but only create CombatEvents for ABSORB
        else if (eventType == "SPELL_MISSED" || eventType == "SPELL_PERIODIC_MISSED") {
            if (tokens.size() >= parser::EventParser<parser::SpellMissedTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);
                ur.timestamp_ms = data.timestamp_ms;
                parsed = false;

                // Register GUID->Name mappings for all miss types
                registerGuidName(data.source_guid, data.source_name);
                registerGuidName(data.dest_guid, data.dest_name);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                // Store MissedEvent for debug dumping (all miss types)
                MissedEvent missedEvent;
                missedEvent.source_guid = std::string(data.source_guid);
                missedEvent.source_name = std::string(data.source_name);
                missedEvent.target_guid = std::string(data.dest_guid);
                missedEvent.target_name = std::string(data.dest_name);
                missedEvent.spell_id = data.spell.spell_id;
                missedEvent.spell_name = std::string(data.spell.spell_name);
                missedEvent.timestamp_ms = data.timestamp_ms;
                missedEvent.miss_type = data.miss_type;
                missedEvent.amount_missed = data.amount_missed;
                missedEvent.blocked_amount = data.blocked_amount;
                missedEvent.is_offhand = data.is_off_hand;
                missedEvent.is_critical = data.critical;
                missedEvent.is_periodic = (eventType == "SPELL_PERIODIC_MISSED");
                result.missedEvents.push_back(std::move(missedEvent));

                // Only create CombatEvents for ABSORB misses (treated as 0-damage hits)
                if (data.miss_type == MissType::ABSORB) {
                    CombatEvent combatEvent;
                    combatEvent.source_guid_id = guidInterner().intern(data.source_guid);
                    combatEvent.target_guid_id = guidInterner().intern(data.dest_guid);
                    combatEvent.owner_guid_id = 0;
                    combatEvent.timestamp_ms = data.timestamp_ms;
                    combatEvent.spell_id = data.spell.spell_id;
                    combatEvent.amount = 0;
                    combatEvent.effective_amount = 0;
                    combatEvent.absorbed = data.amount_missed;
                    combatEvent.blocked = 0;
                    combatEvent.resisted = 0;
                    combatEvent.spell_school = static_cast<uint8_t>(data.spell.spell_school);
                    combatEvent.event_type = CombatEventType::DamageDealt;

                    uint16_t flags = CombatEventFlags::Absorbed;
                    if (eventType == "SPELL_PERIODIC_MISSED") {
                        flags |= CombatEventFlags::Periodic;
                    }
                    if (data.critical) flags |= CombatEventFlags::Critical;
                    combatEvent.flags = flags;

                    result.combatEvents.push_back(std::move(combatEvent));
                }
            }
        } else if (eventType == "SWING_MISSED") {
            if (tokens.size() >= parser::EventParser<parser::SwingMissedTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingMissedTag>::parse_and_return(tokens);
                ur.timestamp_ms = data.timestamp_ms;
                parsed = false;

                // Register GUID->Name mappings
                registerGuidName(data.source_guid, data.source_name);
                registerGuidName(data.dest_guid, data.dest_name);

                // Store MissedEvent for debug dumping (all miss types)
                MissedEvent missedEvent;
                missedEvent.source_guid = std::string(data.source_guid);
                missedEvent.source_name = std::string(data.source_name);
                missedEvent.target_guid = std::string(data.dest_guid);
                missedEvent.target_name = std::string(data.dest_name);
                missedEvent.spell_id = 1;  // Melee (spell_id=1 is synthetic melee ID)
                missedEvent.spell_name = "Melee";
                missedEvent.timestamp_ms = data.timestamp_ms;
                missedEvent.miss_type = data.miss_type;
                missedEvent.amount_missed = data.amount_missed;
                missedEvent.blocked_amount = 0;
                missedEvent.is_offhand = data.is_off_hand;
                missedEvent.is_critical = data.critical;
                missedEvent.is_periodic = false;
                result.missedEvents.push_back(std::move(missedEvent));

                // Only create CombatEvents for ABSORB misses
                if (data.miss_type == MissType::ABSORB) {
                    CombatEvent combatEvent;
                    combatEvent.source_guid_id = guidInterner().intern(data.source_guid);
                    combatEvent.target_guid_id = guidInterner().intern(data.dest_guid);
                    combatEvent.owner_guid_id = 0;
                    combatEvent.timestamp_ms = data.timestamp_ms;
                    combatEvent.spell_id = 1;  // Melee (spell_id=1 is synthetic melee ID)
                    combatEvent.amount = 0;
                    combatEvent.effective_amount = 0;
                    combatEvent.absorbed = data.amount_missed;
                    combatEvent.blocked = 0;
                    combatEvent.resisted = 0;
                    combatEvent.spell_school = static_cast<uint8_t>(SpellSchool::Physical);
                    combatEvent.event_type = CombatEventType::DamageDealt;

                    uint16_t flags = CombatEventFlags::Absorbed | CombatEventFlags::IsSwing;
                    if (data.is_off_hand) flags |= CombatEventFlags::OffHand;
                    if (data.critical) flags |= CombatEventFlags::Critical;
                    combatEvent.flags = flags;

                    result.combatEvents.push_back(std::move(combatEvent));
                }
            }
        }
        // Store absorb events (shields absorbing damage)
        else if (eventType == "SPELL_ABSORBED") {
            if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedTag>::minimum_token_count()) {
                auto data = parser::EventParser<parser::SpellAbsorbedTag>::parse_and_return(tokens);
                ur.timestamp_ms = data.timestamp_ms;
                parsed = false;

                // Register GUID->Name mappings
                registerGuidName(data.source_guid, data.source_name);
                registerGuidName(data.dest_guid, data.dest_name);
                registerGuidName(data.absorber_guid, data.absorber_name);
                if (data.spell.spell_id > 0) {
                    registerSpellName(data.spell.spell_id, data.spell.spell_name);
                }
                registerSpellName(data.absorb_spell_id, data.absorb_spell_name);

                AbsorbEvent absorbEvent;
                absorbEvent.source_guid = std::string(data.source_guid);
                absorbEvent.source_name = std::string(data.source_name);
                absorbEvent.target_guid = std::string(data.dest_guid);
                absorbEvent.target_name = std::string(data.dest_name);
                absorbEvent.absorber_guid = std::string(data.absorber_guid);
                absorbEvent.absorber_name = std::string(data.absorber_name);
                absorbEvent.source_spell_id = data.spell.spell_id;
                absorbEvent.source_spell_name = std::string(data.spell.spell_name);
                absorbEvent.absorb_spell_id = data.absorb_spell_id;
                absorbEvent.absorb_spell_name = std::string(data.absorb_spell_name);
                absorbEvent.timestamp_ms = data.timestamp_ms;
                absorbEvent.absorbed_amount = data.amount;
                absorbEvent.total_damage = 0;  // Not available in SPELL_ABSORBED
                absorbEvent.supporter_guid = "";
                result.absorbEvents.push_back(std::move(absorbEvent));

                // Create CombatEvent for absorb as healing (credited to shield caster)
                auto combatEvent = createHealCombatEvent(
                    data.absorber_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.absorb_spell_id, data.amount,
                    0,  // absorbed = 0 (this IS the absorb, not a heal that got absorbed)
                    0,  // overhealing = 0 (only actual absorbed damage counts)
                    static_cast<uint8_t>(data.absorb_spell_school));
                combatEvent.flags = CombatEventFlags::IsAbsorb;
                result.combatEvents.push_back(std::move(combatEvent));
            }
        } else if (eventType == "SPELL_ABSORBED_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedSupportTag>::minimum_token_count()) {
                auto data = parser::EventParser<parser::SpellAbsorbedSupportTag>::parse_and_return(tokens);
                ur.timestamp_ms = data.timestamp_ms;
                parsed = false;

                // Register GUID->Name mappings
                registerGuidName(data.source_guid, data.source_name);
                registerGuidName(data.dest_guid, data.dest_name);
                registerGuidName(data.absorber_guid, data.absorber_name);
                if (data.spell.spell_id > 0) {
                    registerSpellName(data.spell.spell_id, data.spell.spell_name);
                }
                registerSpellName(data.absorb_spell_id, data.absorb_spell_name);

                AbsorbEvent absorbEvent;
                absorbEvent.source_guid = std::string(data.source_guid);
                absorbEvent.source_name = std::string(data.source_name);
                absorbEvent.target_guid = std::string(data.dest_guid);
                absorbEvent.target_name = std::string(data.dest_name);
                absorbEvent.absorber_guid = std::string(data.absorber_guid);
                absorbEvent.absorber_name = std::string(data.absorber_name);
                absorbEvent.source_spell_id = data.spell.spell_id;
                absorbEvent.source_spell_name = std::string(data.spell.spell_name);
                absorbEvent.absorb_spell_id = data.absorb_spell_id;
                absorbEvent.absorb_spell_name = std::string(data.absorb_spell_name);
                absorbEvent.timestamp_ms = data.timestamp_ms;
                absorbEvent.absorbed_amount = data.amount;
                absorbEvent.total_damage = 0;  // Not available in SPELL_ABSORBED_SUPPORT
                absorbEvent.supporter_guid = std::string(data.supporter_guid);
                result.absorbEvents.push_back(std::move(absorbEvent));

                // Create CombatEvent for absorb as healing (credited to shield caster)
                auto combatEvent = createHealCombatEvent(
                    data.absorber_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.absorb_spell_id, data.amount,
                    0, 0, static_cast<uint8_t>(data.absorb_spell_school));
                combatEvent.flags = CombatEventFlags::IsAbsorb | CombatEventFlags::Support;
                result.combatEvents.push_back(std::move(combatEvent));
            }
        }
        else if (eventType == "SPELL_HEAL") {
            if (tokens.size() >= parser::EventParser<parser::SpellHealTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createHealCombatEvent(
                    data.source_guid, data.dest_guid, data.advanced_info.owner_guid,
                    data.timestamp_ms, data.spell.spell_id, data.amount,
                    data.absorbed, data.overhealing,
                    static_cast<uint8_t>(data.spell.spell_school));
                combatEvent.flags = buildHealFlags(data.critical, data.overhealing, data.absorbed);
                result.combatEvents.push_back(std::move(combatEvent));
            }
        } else if (eventType == "SPELL_PERIODIC_HEAL") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicHealTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createHealCombatEvent(
                    data.source_guid, data.dest_guid, data.advanced_info.owner_guid,
                    data.timestamp_ms, data.spell.spell_id, data.amount,
                    data.absorbed, data.overhealing,
                    static_cast<uint8_t>(data.spell.spell_school));
                combatEvent.flags = buildHealFlags(data.critical, data.overhealing, data.absorbed, true);
                result.combatEvents.push_back(std::move(combatEvent));
            }
        }
        // Support events (Augmentation Evoker)
        // Support events attribute damage/healing to the supporter
        // (e.g. Aug Evoker). The parent damage/heal event still credits
        // the full amp'd amount to the caster; we build the shadow
        // record for Aug AND subtract the shadow's amounts from the
        // parent so the caster's row ends up net of Aug's contribution
        // and the meter sum matches raid damage/healing dealt.
        else if (eventType == "SPELL_DAMAGE_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDamageSupportTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createDamageCombatEvent(
                    data.supporter_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.spell.spell_id, data.amount, data.mitigated_amount,
                    data.overkill, data.absorbed, data.blocked, data.resisted,
                    static_cast<uint8_t>(data.damage_school), data.critical);
                combatEvent.flags = buildDamageFlags(
                    data.critical, data.overkill, data.absorbed, data.blocked, data.resisted,
                    data.glancing, data.crushing, false, false, false, true);  // is_support = true
                recordSupport(std::move(combatEvent), data.source_guid, /*is_swing=*/false);
            }
        } else if (eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicDamageSupportTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createDamageCombatEvent(
                    data.supporter_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.spell.spell_id, data.amount, data.mitigated_amount,
                    data.overkill, data.absorbed, data.blocked, data.resisted,
                    static_cast<uint8_t>(data.damage_school), data.critical);
                // is_periodic = false to match WCL (doesn't set periodic for support events)
                combatEvent.flags = buildDamageFlags(
                    data.critical, data.overkill, data.absorbed, data.blocked, data.resisted,
                    false, false, false, false, false, true);  // is_periodic = false, is_support = true
                recordSupport(std::move(combatEvent), data.source_guid, /*is_swing=*/false);
            }
        } else if (eventType == "SWING_DAMAGE_LANDED_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingDamageLandedSupportTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createDamageCombatEvent(
                    data.supporter_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.spell.spell_id, data.amount, data.mitigated_amount,
                    data.overkill, data.absorbed, data.blocked, data.resisted,
                    static_cast<uint8_t>(data.spell.spell_school), data.critical);
                combatEvent.flags = buildDamageFlags(
                    data.critical, data.overkill, data.absorbed, data.blocked, data.resisted,
                    data.glancing, data.crushing, false, false, true, true);  // is_swing = true, is_support = true
                recordSupport(std::move(combatEvent), data.source_guid, /*is_swing=*/true);
            }
        }
        // WCL does not set the periodic flag on support events, even for periodic heals.
        // WCL returns amount=effective, overheal=wasted; local logs have amount=total, overhealing=wasted
        // So we pass overhealing to get correct effective_amount calculation
        else if (eventType == "SPELL_HEAL_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellHealSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellHealSupportTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createHealCombatEvent(
                    data.supporter_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.spell.spell_id, data.amount,
                    data.absorbed, data.overhealing,
                    static_cast<uint8_t>(data.spell.spell_school));
                // is_periodic = false to match WCL (doesn't set periodic for support events)
                combatEvent.flags = buildHealFlags(data.critical, data.overhealing, data.absorbed, false, true);
                recordSupport(std::move(combatEvent), data.source_guid, /*is_swing=*/false);
            }
        } else if (eventType == "SPELL_PERIODIC_HEAL_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicHealSupportTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                registerSpellName(data.spell.spell_id, data.spell.spell_name);

                auto combatEvent = createHealCombatEvent(
                    data.supporter_guid, data.dest_guid, std::string_view{},
                    data.timestamp_ms, data.spell.spell_id, data.amount,
                    data.absorbed, data.overhealing,
                    static_cast<uint8_t>(data.spell.spell_school));
                // is_periodic = false to match WCL (doesn't set periodic for support events)
                combatEvent.flags = buildHealFlags(data.critical, data.overhealing, data.absorbed, false, true);
                recordSupport(std::move(combatEvent), data.source_guid, /*is_swing=*/false);
            }
        }
        else if (eventType == "SPELL_CAST_START") {
            if (tokens.size() >= parser::EventParser<parser::SpellCastStartTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellCastStartTag>::parse_and_return(tokens);
                std::string actorGuid = std::string(data.source_guid);

                // Check if this player has a pending cast - if so, they cancelled it
                auto it = pendingCastStarts.find(actorGuid);
                if (it != pendingCastStarts.end()) {
                    recordCancelledCast(actorGuid, it->second, data.timestamp_ms);
                }
                pendingCastStarts[actorGuid] = EventExtractorContext::PendingCast{ data.spell.spell_id, data.timestamp_ms };
            }
            continue;
        }
        else if (eventType == "SPELL_CAST_FAILED") {
            // A failure of the spell being cast ends it. Failures of other
            // spells (an off-GCD button pressed mid-cast) leave it running.
            if (tokens.size() >= parser::EventParser<parser::SpellCastFailedTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellCastFailedTag>::parse_and_return(tokens);
                std::string actorGuid = std::string(data.source_guid);
                auto it = pendingCastStarts.find(actorGuid);
                if (it != pendingCastStarts.end() && it->second.spell_id == data.spell.spell_id) {
                    recordCancelledCast(actorGuid, it->second, data.timestamp_ms);
                    pendingCastStarts.erase(it);
                }
            }
            continue;
        }
        else if (eventType == "SPELL_CAST_SUCCESS") {
            if (tokens.size() >= parser::EventParser<parser::SpellCastSuccessTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellCastSuccessTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                spell_id = data.spell.spell_id;
                spell_name = std::string(data.spell.spell_name);
                isSpellCastSuccess = true;
                registerSpellName(spell_id, data.spell.spell_name);

                // Generate cooldown reductions for resource-based modifiers
                if (data.advanced_info.power_cost > 0) {
                    std::string actorGuid = std::string(data.source_guid);
                    auto actorIt = actorResourceModifiers.find(actorGuid);
                    if (actorIt != actorResourceModifiers.end()) {
                        // Check if this actor has modifiers for this power type
                        auto powerTypeIt = actorIt->second.modifiers_by_power_type.find(data.advanced_info.power_type);
                        if (powerTypeIt != actorIt->second.modifiers_by_power_type.end()) {
                            // Intern the actor GUID once for all reductions
                            uint32_t actorGuidId = guidInterner().intern(data.source_guid);

                            for (const auto& modifier : powerTypeIt->second) {
                                if (modifier.resource.resource_per_second <= 0) continue;
                                // Reduction formula: (power_cost / resource_per_second) * 1000 ms
                                int32_t reduction_ms = (data.advanced_info.power_cost * 1000) / modifier.resource.resource_per_second;

                                CooldownReductionRecord record;
                                record.actor_guid_id = actorGuidId;
                                record.spell_id = modifier.modified_spell_id;
                                record.timestamp_ms = data.timestamp_ms;
                                record.reduction_ms = reduction_ms;

                                result.cooldownReductions.push_back(record);

                            }
                        }
                    }
                }
            }
        }
        else if (eventType == "SPELL_ENERGIZE") {
            if (tokens.size() >= parser::EventParser<parser::SpellEnergizeTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellEnergizeTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                recordEnergize(result, data);
            }
        } else if (eventType == "SPELL_PERIODIC_ENERGIZE") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicEnergizeTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicEnergizeTag>::parse_and_return(tokens);
                notePositionedEvent(data);
                recordEnergize(result, data);
            }
        }
        // Summons record pet and guardian ownership
        else if (eventType == "SPELL_SUMMON") {
            if (tokens.size() >= parser::EventParser<parser::SpellSummonTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellSummonTag>::parse_and_return(tokens);

                // Register GUID->Name for summon events (critical for pet/guardian names)
                registerGuidName(data.source_guid, data.source_name);
                registerGuidName(data.dest_guid, data.dest_name);

                SummonEvent summon;
                summon.summoner_guid = std::string(data.source_guid);
                summon.summoned_guid = std::string(data.dest_guid);
                summon.timestamp_ms = data.timestamp_ms;
                summon.spell_id = data.spell.spell_id;

                result.summonEvents.push_back(std::move(summon));
            }
            continue;  // SPELL_SUMMON doesn't have position data, skip rest of processing
        }
#ifndef NDEBUG
        else {
            parser::debug::recordUnhandledEvent(eventType);
        }
#endif

        if (!parsed) {
            continue;
        }

        // Extract spell cast event (only for SPELL_CAST_SUCCESS)
        if (isSpellCastSuccess) {
            SpellCastEvent cast;
            cast.actor_guid = std::string(common.source_guid);
            cast.target_guid = std::string(common.dest_guid);
            cast.spell_id = spell_id;
            cast.end_timestamp_ms = ur.timestamp_ms;
            cast.cancelled = false;  // Successful cast

            // Look for matching SPELL_CAST_START to get cast duration
            // Key is just actor_guid (only one pending cast per player)
            auto startIt = pendingCastStarts.find(cast.actor_guid);
            if (startIt != pendingCastStarts.end() && startIt->second.spell_id == spell_id) {
                // Matching spell - this was a cast-time spell
                cast.start_timestamp_ms = startIt->second.start_timestamp_ms;
                pendingCastStarts.erase(startIt);  // Remove used entry
            } else {
                // Instant cast. A different spell succeeding does not end the
                // pending cast: off-GCD spells (Combustion during Pyroblast)
                // go off mid-cast. The pending cast ends on its own SUCCESS or
                // FAILED, or on the next CAST_START.
                cast.start_timestamp_ms = cast.end_timestamp_ms;
            }

            int32_t hasteAtCast = hasteTracker.getHasteAtTime(cast.actor_guid, cast.end_timestamp_ms);
            cast.haste_permille = haste::HasteTracker::permilleToTenthsPercent(hasteAtCast);

            // Record spell to spellsSeenByClass (for UI spell assignment config)
            auto specIt = specIdCache.find(cast.actor_guid);
            if (specIt != specIdCache.end() && specIt->second > 0) {
                parser::WowClass wowClass = parser::getClassFromSpecId(specIt->second);
                if (wowClass != parser::WowClass::Unknown && !spell_name.empty()) {
                    result.spellsSeenByClass.recordSpell(wowClass, spell_id, spell_name);
                }
            }

            result.spellCastEvents.push_back(std::move(cast));
        }

        // The advanced block describes the unit named by info_guid: its
        // health and resources, and the position extracted above. That unit
        // is the event's source or dest; anything else is skipped.
        if (!common.has_advanced_info) {
            continue;
        }

        const std::string_view infoGuid = common.info_guid;
        std::string_view infoName;
        UnitFlags infoFlags;
        if (infoGuid == common.source_guid) {
            infoName = common.source_name;
            infoFlags = common.source_flags;
        } else if (infoGuid == common.dest_guid) {
            infoName = common.dest_name;
            infoFlags = common.dest_flags;
        } else {
            continue;
        }

        // Skip non-existent units (SPECIAL_NONE flag) and unknown GUID kinds
        const ActorType infoType = getActorTypeFromGuid(infoGuid);
        if (!infoFlags.exists || infoType == ActorType::Unknown) {
            continue;
        }

        {
            ResourceEvent resEvent;
            resEvent.actor_guid = std::string(infoGuid);
            resEvent.actor_type = infoType;
            resEvent.flags = infoFlags;
            resEvent.timestamp_ms = ur.timestamp_ms;
            resEvent.current_health = common.current_hp;
            resEvent.max_health = common.max_hp;
            // Advanced lines carry the caster's resources, so use them when
            // this event is about the unit that owns them. Entry 0 is the
            // primary; anything after it is a secondary resource.
            resEvent.power_type = -1;  // -1 indicates no power data
            resEvent.current_power = 0;
            resEvent.max_power = 0;
            if (common.power_count > 0 && infoGuid == common.source_guid) {
                resEvent.power_type = common.powers[0].type;
                resEvent.current_power = common.powers[0].current;
                resEvent.max_power = common.powers[0].max;
                for (uint8_t i = 1; i < common.power_count; ++i) {
                    resEvent.secondary_powers.push_back(
                        {common.powers[i].type, common.powers[i].current,
                         common.powers[i].max});
                }
            }

            result.resourceEvents.push_back(std::move(resEvent));
        }

        // The views below outlive the file mapping (flows release it once
        // the databases are built), so point them at interned copies.
        ActorEvent event;
        event.source_guid = guidInterner().lookup(guidInterner().intern(infoGuid));
        event.source_actor = actorNameInterner().lookup(actorNameInterner().intern(infoName));
        event.actor_type = infoType;
        event.flags = infoFlags;
        event.timestamp_ms = ur.timestamp_ms;
        event.x_loc = ur.x_loc;
        event.y_loc = ur.y_loc;
        event.map_id = ur.map_id;
        event.facing_radians = ur.radians;
        // Track first-seen timestamp (for AuraDatabase removal-only buff backdating)
        registerFirstSeen(infoGuid, ur.timestamp_ms);

        result.actorEvents.push_back(std::move(event));
    }

    // Handle any orphaned pending casts (encounter ended mid-cast)
    // These are casts that started but never completed
    if (!pendingCastStarts.empty()) {
        // Use the timestamp of the last actor event as the "end" time for orphaned casts
        int32_t endTime = 0;
        if (!result.actorEvents.empty()) {
            endTime = result.actorEvents.back().timestamp_ms;
        } else if (!result.spellCastEvents.empty()) {
            endTime = result.spellCastEvents.back().end_timestamp_ms;
        }

        for (const auto& [actorGuid, pending] : pendingCastStarts) {
            int32_t castEnd = endTime > pending.start_timestamp_ms ? endTime : pending.start_timestamp_ms + 1;
            recordCancelledCast(actorGuid, pending, castEnd);
        }
    }

    return result;
}
