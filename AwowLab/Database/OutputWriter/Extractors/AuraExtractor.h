#pragma once
#include "../EventExtractorContext.h"
#include "Parser/Parser_Aura.h"
#include "Haste/HasteTracker.h"
#include <unordered_map>

namespace awow::extract {

// Context for aura extraction (holds caches needed across events)
struct AuraExtractorContext {
    ExtractedData& result;
    haste::HasteTracker& hasteTracker;
    const std::unordered_map<std::string, uint16_t>& specIdCache;
    std::function<void(std::string_view, std::string_view)> registerGuidName;
};

// Handle all SPELL_AURA_* events
// Returns true if the event was handled and should be skipped in main loop
inline bool extractAuraEvent(
    std::string_view eventType,
    const std::vector<std::string_view>& tokens,
    AuraExtractorContext& ctx
) {
    if (eventType == "SPELL_AURA_APPLIED") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraAppliedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraAppliedTag>::parse_and_return(tokens);

            ctx.registerGuidName(data.source_guid, data.source_name);
            ctx.registerGuidName(data.dest_guid, data.dest_name);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::Applied;
            aura.stacks = 1;

            // Track haste buff application
            ctx.hasteTracker.applyBuff(aura.target_guid, aura.spell_id, 1, aura.timestamp_ms);

            // Record aura to aurasSeenByClass (by target's class)
            auto targetSpecIt = ctx.specIdCache.find(aura.target_guid);
            if (targetSpecIt != ctx.specIdCache.end() && targetSpecIt->second > 0) {
                parser::WowClass wowClass = parser::getClassFromSpecId(targetSpecIt->second);
                if (wowClass != parser::WowClass::Unknown && !aura.spell_name.empty()) {
                    bool is_buff = (aura.aura_type == AuraType::BUFF);
                    ctx.result.aurasSeenByClass.recordAura(wowClass, aura.spell_id, aura.spell_name, is_buff);
                }
            }

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    if (eventType == "SPELL_AURA_REMOVED") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraRemovedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraRemovedTag>::parse_and_return(tokens);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::Removed;
            aura.stacks = 1;

            ctx.hasteTracker.removeBuff(aura.target_guid, aura.spell_id, aura.timestamp_ms);

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    if (eventType == "SPELL_AURA_REFRESH") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraRefreshTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraRefreshTag>::parse_and_return(tokens);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::Refresh;
            aura.stacks = 1;

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    if (eventType == "SPELL_AURA_APPLIED_DOSE") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraAppliedDoseTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraAppliedDoseTag>::parse_and_return(tokens);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::AppliedDose;
            aura.stacks = static_cast<uint8_t>(data.amount);

            ctx.hasteTracker.updateBuffStacks(aura.target_guid, aura.spell_id, aura.stacks, aura.timestamp_ms);

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    if (eventType == "SPELL_AURA_REMOVED_DOSE") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraRemovedDoseTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraRemovedDoseTag>::parse_and_return(tokens);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::RemovedDose;
            aura.stacks = static_cast<uint8_t>(data.amount);

            ctx.hasteTracker.updateBuffStacks(aura.target_guid, aura.spell_id, aura.stacks, aura.timestamp_ms);

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    if (eventType == "SPELL_AURA_BROKEN") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraBrokenTag>::parse_and_return(tokens);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::Broken;
            aura.stacks = 1;

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    if (eventType == "SPELL_AURA_BROKEN_SPELL") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenSpellTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraBrokenSpellTag>::parse_and_return(tokens);

            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);  // Who broke the CC
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = data.timestamp_ms;
            aura.event_type = AuraEventType::BrokenSpell;
            aura.stacks = 1;
            // Capture the spell that broke the CC
            aura.breaking_spell_id = data.extra_spell_id;
            aura.breaking_spell_name = std::string(data.extra_spell_name);

            ctx.result.auraEvents.push_back(std::move(aura));
        }
        return true;
    }

    return false;
}

} // namespace awow::extract
