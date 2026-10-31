#pragma once
#include "HasteBuffTable.h"
#include "HasteRatingBuffTable.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

// Each player's haste over an encounter: the base comes from the
// COMBATANT_INFO haste rating, then known haste auras are applied and removed
// as they appear in the log. Values are permille (300 = 30%).
//
// The percentage buffs are looked up in HasteBuffTable, which is empty until
// Database/HasteBuffData.h has loaded the shipped table into it.

namespace haste {

// A single haste change event
struct HasteEvent {
    int32_t timestamp_ms;    // Relative to log start (negative = before encounter)
    int32_t haste_permille;  // Current haste at this point in time
};

// Per-player haste state
struct PlayerHasteState {
    int32_t base_haste_permille = 0;           // From haste rating
    int32_t current_haste_permille = 0;        // Current total haste
    std::vector<HasteEvent> haste_timeline;    // Sorted by timestamp

    // Active percentage buff tracking: spell_id -> (stack_count, haste_per_stack).
    // A buff that does not stack is held at one stack.
    std::unordered_map<uint32_t, std::pair<int32_t, int32_t>> active_buffs;

    // Active rating buff tracking: spell_id -> (stack_count, rating_per_stack)
    std::unordered_map<uint32_t, std::pair<int32_t, int32_t>> active_rating_buffs;
};

class HasteTracker {
public:
    HasteTracker() = default;

    // haste_rating is the raw COMBATANT_INFO value (e.g. 8962)
    void setBaseHasteFromRating(const std::string& player_guid, int32_t haste_rating) {
        auto& state = player_states_[player_guid];
        state.base_haste_permille = hasteRatingToPermille(haste_rating);
        state.current_haste_permille = state.base_haste_permille;

        if (state.haste_timeline.empty()) {
            state.haste_timeline.push_back({ 0, state.current_haste_permille });
        }
    }

    // Returns true if the aura is a known haste buff
    bool applyBuff(const std::string& player_guid, uint32_t spell_id,
                   int32_t stacks, int32_t timestamp_ms) {
        auto& state = player_states_[player_guid];
        bool is_haste_buff = false;

        auto buff_info = getHasteBuff(spell_id);
        if (buff_info) {
            state.active_buffs[spell_id] = { buff_info->stacks() ? stacks : 1,
                                             buff_info->haste_permille };
            is_haste_buff = true;
        }

        auto rating_info = getHasteRating(spell_id);
        if (rating_info) {
            state.active_rating_buffs[spell_id] = { stacks, rating_info->rating_per_stack };
            is_haste_buff = true;
        }

        if (!is_haste_buff) {
            return false;
        }

        recalculateHaste(state);
        state.haste_timeline.push_back({ timestamp_ms, state.current_haste_permille });

        return true;
    }

    // Returns true if the aura is a known haste buff
    bool removeBuff(const std::string& player_guid, uint32_t spell_id, int32_t timestamp_ms) {
        auto& state = player_states_[player_guid];
        bool was_haste_buff = false;

        if (state.active_buffs.erase(spell_id) > 0) {
            was_haste_buff = true;
        }

        if (state.active_rating_buffs.erase(spell_id) > 0) {
            was_haste_buff = true;
        }

        if (!was_haste_buff) {
            return false;
        }

        recalculateHaste(state);
        state.haste_timeline.push_back({ timestamp_ms, state.current_haste_permille });

        return true;
    }

    // For SPELL_AURA_APPLIED_DOSE / SPELL_AURA_REMOVED_DOSE
    bool updateBuffStacks(const std::string& player_guid, uint32_t spell_id,
                          int32_t new_stacks, int32_t timestamp_ms) {
        auto& state = player_states_[player_guid];
        bool is_stacking_buff = false;

        auto buff_info = getHasteBuff(spell_id);
        if (buff_info && buff_info->stacks()) {
            auto it = state.active_buffs.find(spell_id);
            if (it != state.active_buffs.end()) {
                it->second.first = new_stacks;
            } else {
                state.active_buffs[spell_id] = { new_stacks, buff_info->haste_permille };
            }
            is_stacking_buff = true;
        }

        auto rating_info = getHasteRating(spell_id);
        if (rating_info && rating_info->stacks) {
            auto it = state.active_rating_buffs.find(spell_id);
            if (it != state.active_rating_buffs.end()) {
                it->second.first = new_stacks;
            } else {
                state.active_rating_buffs[spell_id] = { new_stacks, rating_info->rating_per_stack };
            }
            is_stacking_buff = true;
        }

        if (!is_stacking_buff) {
            return false;
        }

        recalculateHaste(state);
        state.haste_timeline.push_back({ timestamp_ms, state.current_haste_permille });

        return true;
    }

    // Haste in effect at timestamp_ms, in permille
    int32_t getHasteAtTime(const std::string& player_guid, int32_t timestamp_ms) const {
        auto it = player_states_.find(player_guid);
        if (it == player_states_.end()) {
            return 0;  // Unknown player
        }

        const auto& timeline = it->second.haste_timeline;
        if (timeline.empty()) {
            return it->second.base_haste_permille;
        }

        // Last change at or before timestamp_ms; the timeline is sorted
        int32_t result = timeline[0].haste_permille;
        for (const auto& event : timeline) {
            if (event.timestamp_ms <= timestamp_ms) {
                result = event.haste_permille;
            } else {
                break;
            }
        }

        return result;
    }

    int32_t getCurrentHaste(const std::string& player_guid) const {
        auto it = player_states_.find(player_guid);
        if (it == player_states_.end()) {
            return 0;
        }
        return it->second.current_haste_permille;
    }

    // Convert permille to percentage with one decimal (e.g., 305 -> 30.5)
    static float permilleToPercent(int32_t permille) {
        return static_cast<float>(permille) / 10.0f;
    }

    // Permille already is tenths of a percent; this only clamps negatives
    // for the unsigned SpellCastRecord field.
    static uint16_t permilleToTenthsPercent(int32_t permille) {
        return static_cast<uint16_t>(permille > 0 ? permille : 0);
    }

    bool hasPlayer(const std::string& player_guid) const {
        return player_states_.find(player_guid) != player_states_.end();
    }

    void clear() {
        player_states_.clear();
    }

private:
    void recalculateHaste(PlayerHasteState& state) {
        int32_t base_haste = state.base_haste_permille;

        int32_t total_rating = 0;
        for (const auto& [spell_id, buff_data] : state.active_rating_buffs) {
            int32_t stacks = buff_data.first;
            int32_t rating_per_stack = buff_data.second;

            int32_t buff_rating;
            if (rating_per_stack > 0) {
                buff_rating = rating_per_stack * stacks;
            } else {
                // Non-stacking buff - look up flat value
                auto info = getHasteRating(spell_id);
                buff_rating = info ? info->rating : 0;
            }

            total_rating += buff_rating;
        }

        // The base is already permille, so only the proc rating is converted
        int32_t rating_permille = hasteRatingToPermille(total_rating);
        int32_t haste_with_rating = base_haste + rating_permille;

        // Apply percentage buffs multiplicatively on top
        int32_t total = haste_with_rating;
        for (const auto& [spell_id, buff_data] : state.active_buffs) {
            const int32_t stacks = buff_data.first;
            const int32_t per_stack = buff_data.second;
            total = addHasteMultiplicative(total, per_stack * stacks);
        }

        state.current_haste_permille = total;
    }

    std::unordered_map<std::string, PlayerHasteState> player_states_;
};

} // namespace haste
