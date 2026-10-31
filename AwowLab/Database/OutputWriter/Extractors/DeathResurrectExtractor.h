#pragma once
#include "../EventExtractorContext.h"
#include "Core/Utils/ColorUtils.h"  // For awow::getActorTypeFromGuid
#include "Parser/Parser_Unit.h"
#include <iostream>

namespace awow::extract {

// A unit killed by SPELL_INSTAKILL also gets a UNIT_DIED for the same death,
// a moment apart. Nothing can die twice that fast, so a second death for the
// same unit inside this window is the same death.
inline constexpr int32_t kSameDeathWindowMs = 1000;

// Add a death, folding it into an earlier record of the same death. The
// merged record keeps the earlier timestamp and the instakill's spell.
inline void recordDeath(ExtractedData& result, DeathEvent&& death) {
    for (auto it = result.deathEvents.rbegin(); it != result.deathEvents.rend(); ++it) {
        if (death.timestamp_ms - it->timestamp_ms > kSameDeathWindowMs) break;
        if (it->actor_guid != death.actor_guid) continue;
        if (it->killing_spell_id == 0 && death.killing_spell_id != 0) {
            it->killing_spell_id = death.killing_spell_id;
            it->killing_spell_name = std::move(death.killing_spell_name);
        }
        return;
    }
    result.deathEvents.push_back(std::move(death));
}

// Handle UNIT_DIED, UNIT_DESTROYED, SPELL_INSTAKILL, SPELL_RESURRECT events
// Returns true if the event was handled and should be skipped in main loop
inline bool extractDeathResurrectEvent(
    std::string_view eventType,
    const std::vector<std::string_view>& tokens,
    ExtractedData& result,
    const std::function<void(std::string_view, std::string_view)>& registerGuidName
) {
    if (eventType == "UNIT_DIED") {
        if (tokens.size() >= parser::EventParser<parser::UnitDiedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::UnitDiedTag>::parse_and_return(tokens);

            registerGuidName(data.dest_guid, data.dest_name);

            DeathEvent death;
            death.actor_guid = std::string(data.dest_guid);
            death.actor_name = std::string(data.dest_name);
            death.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            death.timestamp_ms = data.timestamp_ms;
            death.killing_spell_id = 0;
            death.killing_spell_name = "";

            recordDeath(result, std::move(death));
        }
        return true;
    }

    if (eventType == "UNIT_DESTROYED") {
        if (tokens.size() >= parser::EventParser<parser::UnitDestroyedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::UnitDestroyedTag>::parse_and_return(tokens);

            registerGuidName(data.dest_guid, data.dest_name);

            DeathEvent death;
            death.actor_guid = std::string(data.dest_guid);
            death.actor_name = std::string(data.dest_name);
            death.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            death.timestamp_ms = data.timestamp_ms;
            death.killing_spell_id = 0;
            death.killing_spell_name = "";

            recordDeath(result, std::move(death));
        }
        return true;
    }

    if (eventType == "SPELL_INSTAKILL") {
        if (tokens.size() >= parser::EventParser<parser::SpellInstakillTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellInstakillTag>::parse_and_return(tokens);

            registerGuidName(data.dest_guid, data.dest_name);

            DeathEvent death;
            death.actor_guid = std::string(data.dest_guid);
            death.actor_name = std::string(data.dest_name);
            death.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            death.timestamp_ms = data.timestamp_ms;
            death.killing_spell_id = data.spell_id;
            death.killing_spell_name = std::string(data.spell_name);

            recordDeath(result, std::move(death));
        }
        return true;
    }

    if (eventType == "SPELL_RESURRECT") {
        if (tokens.size() >= parser::EventParser<parser::SpellResurrectTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellResurrectTag>::parse_and_return(tokens);

            registerGuidName(data.dest_guid, data.dest_name);

            ResurrectEvent resurrect;
            resurrect.actor_guid = std::string(data.dest_guid);
            resurrect.actor_name = std::string(data.dest_name);
            resurrect.timestamp_ms = data.timestamp_ms;
            resurrect.spell_id = data.spell.spell_id;

            result.resurrectEvents.push_back(std::move(resurrect));
        }
        return true;
    }

    return false;
}

} // namespace awow::extract
