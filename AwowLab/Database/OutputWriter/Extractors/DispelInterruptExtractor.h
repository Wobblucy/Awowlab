#pragma once
#include "../EventExtractorContext.h"
#include "Parser/Parser_Dispel.h"

namespace awow::extract {

// Handle SPELL_DISPEL, SPELL_INTERRUPT, SPELL_STOLEN, SPELL_DISPEL_FAILED events
// Returns true if the event was handled and should be skipped in main loop
inline bool extractDispelInterruptEvent(
    std::string_view eventType,
    const std::vector<std::string_view>& tokens,
    ExtractedData& result,
    const std::function<void(uint32_t, std::string_view)>& registerSpellName,
    const std::function<void(std::string_view, std::string_view)>& registerGuidName
) {
    if (eventType == "SPELL_DISPEL") {
        if (tokens.size() >= parser::EventParser<parser::SpellDispelTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellDispelTag>::parse_and_return(tokens);

            registerGuidName(data.source_guid, data.source_name);
            registerGuidName(data.dest_guid, data.dest_name);
            registerSpellName(data.spell.spell_id, data.spell.spell_name);
            registerSpellName(data.extra_spell_id, data.extra_spell_name);

            DispelInterruptEvent event;
            event.source_guid = std::string(data.source_guid);
            event.source_name = std::string(data.source_name);
            event.target_guid = std::string(data.dest_guid);
            event.target_name = std::string(data.dest_name);
            event.timestamp_ms = data.timestamp_ms;
            event.spell_id = data.spell.spell_id;
            event.spell_name = std::string(data.spell.spell_name);
            event.extra_spell_id = data.extra_spell_id;
            event.extra_spell_name = std::string(data.extra_spell_name);
            event.extra_spell_school = data.extra_spell_school;
            event.event_type = DispelInterruptEvent::EventType::Dispel;
            event.aura_type = data.aura_type;

            result.dispelInterruptEvents.push_back(std::move(event));
        }
        return true;
    }

    if (eventType == "SPELL_INTERRUPT") {
        if (tokens.size() >= parser::EventParser<parser::SpellInterruptTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellInterruptTag>::parse_and_return(tokens);

            registerGuidName(data.source_guid, data.source_name);
            registerGuidName(data.dest_guid, data.dest_name);
            registerSpellName(data.spell.spell_id, data.spell.spell_name);
            registerSpellName(data.extra_spell_id, data.extra_spell_name);

            DispelInterruptEvent event;
            event.source_guid = std::string(data.source_guid);
            event.source_name = std::string(data.source_name);
            event.target_guid = std::string(data.dest_guid);
            event.target_name = std::string(data.dest_name);
            event.timestamp_ms = data.timestamp_ms;
            event.spell_id = data.spell.spell_id;
            event.spell_name = std::string(data.spell.spell_name);
            event.extra_spell_id = data.extra_spell_id;
            event.extra_spell_name = std::string(data.extra_spell_name);
            event.extra_spell_school = data.extra_spell_school;
            event.event_type = DispelInterruptEvent::EventType::Interrupt;
            event.aura_type = AuraType::Unknown;

            result.dispelInterruptEvents.push_back(std::move(event));
        }
        return true;
    }

    if (eventType == "SPELL_STOLEN") {
        if (tokens.size() >= parser::EventParser<parser::SpellStolenTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellStolenTag>::parse_and_return(tokens);

            registerGuidName(data.source_guid, data.source_name);
            registerGuidName(data.dest_guid, data.dest_name);
            registerSpellName(data.spell.spell_id, data.spell.spell_name);
            registerSpellName(data.extra_spell_id, data.extra_spell_name);

            DispelInterruptEvent event;
            event.source_guid = std::string(data.source_guid);
            event.source_name = std::string(data.source_name);
            event.target_guid = std::string(data.dest_guid);
            event.target_name = std::string(data.dest_name);
            event.timestamp_ms = data.timestamp_ms;
            event.spell_id = data.spell.spell_id;
            event.spell_name = std::string(data.spell.spell_name);
            event.extra_spell_id = data.extra_spell_id;
            event.extra_spell_name = std::string(data.extra_spell_name);
            event.extra_spell_school = data.extra_spell_school;
            event.event_type = DispelInterruptEvent::EventType::Stolen;
            event.aura_type = data.aura_type;

            result.dispelInterruptEvents.push_back(std::move(event));
        }
        return true;
    }

    if (eventType == "SPELL_DISPEL_FAILED") {
        if (tokens.size() >= parser::EventParser<parser::SpellDispelFailedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellDispelFailedTag>::parse_and_return(tokens);

            registerGuidName(data.source_guid, data.source_name);
            registerGuidName(data.dest_guid, data.dest_name);
            registerSpellName(data.spell.spell_id, data.spell.spell_name);

            DispelInterruptEvent event;
            event.source_guid = std::string(data.source_guid);
            event.source_name = std::string(data.source_name);
            event.target_guid = std::string(data.dest_guid);
            event.target_name = std::string(data.dest_name);
            event.timestamp_ms = data.timestamp_ms;
            event.spell_id = data.spell.spell_id;
            event.spell_name = std::string(data.spell.spell_name);
            event.extra_spell_id = 0;
            event.extra_spell_name = "";
            event.extra_spell_school = 0;
            event.event_type = DispelInterruptEvent::EventType::DispelFailed;
            event.aura_type = AuraType::Unknown;

            result.dispelInterruptEvents.push_back(std::move(event));
        }
        return true;
    }

    return false;
}

} // namespace awow::extract
