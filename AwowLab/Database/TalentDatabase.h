#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include "Parser/Include/Parser/Parser_Combatant_Info.h"
#include "EventTypes.h"

namespace awow {

    // Per-player talent set with quick lookup
    struct PlayerTalentSet {
        std::string player_guid;
        uint16_t spec_id = 0;
        std::unordered_set<uint32_t> talent_spell_ids;  // Quick O(1) lookup: "does player have talent X?"
        std::vector<parser::TalentEntry> talents;       // Full talent data with ranks
    };

    // Database storing per-player talent configurations
    // Populated from COMBATANT_INFO events at encounter start
    class TalentDatabase {
    public:
        TalentDatabase() = default;
        ~TalentDatabase() = default;

        // Load talent data from COMBATANT_INFO events
        void loadFromCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo);

        // Query: Does player have this talent spell?
        bool playerHasTalent(const std::string& player_guid, uint32_t talent_spell_id) const;

        const PlayerTalentSet* getPlayerTalents(const std::string& player_guid) const;

        const std::unordered_set<uint32_t>* getPlayerTalentSpells(const std::string& player_guid) const;

        // Get talent rank (returns 0 if player doesn't have the talent)
        uint8_t getTalentRank(const std::string& player_guid, uint32_t talent_spell_id) const;

        // Get spec ID for a player (returns 0 if not found)
        uint16_t getPlayerSpecId(const std::string& player_guid) const;

        void clear();

        size_t playerCount() const { return player_talents_.size(); }

        // Get total number of talents across all players
        size_t totalTalentCount() const;

    private:
        // Map: player GUID -> talent set
        std::unordered_map<std::string, PlayerTalentSet> player_talents_;
    };

} // namespace awow
