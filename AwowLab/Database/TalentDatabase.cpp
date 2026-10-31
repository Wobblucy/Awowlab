#include "TalentDatabase.h"
#include <iostream>

namespace awow {

    void TalentDatabase::loadFromCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo) {
        clear();

        // Load talents for each player
        for (const auto& info : combatantInfo) {
            if (info.player_guid.empty()) {
                continue;
            }

            PlayerTalentSet talentSet;
            talentSet.player_guid = info.player_guid;
            talentSet.spec_id = info.spec_id;
            talentSet.talents = info.talents;  // Copy talent entries

            // Build spell ID set for quick lookup
            // Both the talent_id (WCL entry id) and the spell_id are stored, so either matches
            for (const auto& talent : info.talents) {
                if (talent.spell_id > 0) {
                    talentSet.talent_spell_ids.insert(talent.spell_id);
                }
                if (talent.talent_id > 0 && talent.talent_id != talent.spell_id) {
                    talentSet.talent_spell_ids.insert(talent.talent_id);
                }
            }

            player_talents_[info.player_guid] = std::move(talentSet);
        }
    }

    bool TalentDatabase::playerHasTalent(const std::string& player_guid, uint32_t talent_spell_id) const {
        auto it = player_talents_.find(player_guid);
        if (it == player_talents_.end()) {
            return false;
        }

        return it->second.talent_spell_ids.count(talent_spell_id) > 0;
    }

    const PlayerTalentSet* TalentDatabase::getPlayerTalents(const std::string& player_guid) const {
        auto it = player_talents_.find(player_guid);
        if (it == player_talents_.end()) {
            return nullptr;
        }

        return &it->second;
    }

    const std::unordered_set<uint32_t>* TalentDatabase::getPlayerTalentSpells(const std::string& player_guid) const {
        auto it = player_talents_.find(player_guid);
        if (it == player_talents_.end()) {
            return nullptr;
        }

        return &it->second.talent_spell_ids;
    }

    uint8_t TalentDatabase::getTalentRank(const std::string& player_guid, uint32_t talent_spell_id) const {
        auto it = player_talents_.find(player_guid);
        if (it == player_talents_.end()) {
            return 0;
        }

        // Search through talents for matching spell ID
        for (const auto& talent : it->second.talents) {
            if (talent.spell_id == talent_spell_id) {
                return talent.rank;
            }
        }

        return 0;  // Not found
    }

    uint16_t TalentDatabase::getPlayerSpecId(const std::string& player_guid) const {
        auto it = player_talents_.find(player_guid);
        if (it == player_talents_.end()) {
            return 0;
        }

        return it->second.spec_id;
    }

    void TalentDatabase::clear() {
        player_talents_.clear();
    }

    size_t TalentDatabase::totalTalentCount() const {
        size_t total = 0;
        for (const auto& [guid, talentSet] : player_talents_) {
            total += talentSet.talents.size();
        }
        return total;
    }

} // namespace awow
