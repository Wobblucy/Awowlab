#pragma once
#include <cstdint>
#include <unordered_map>
#include <optional>

// Haste buffs that grant rating (trinket procs, set bonuses). Percentage
// buffs such as Bloodlust live in HasteBuffTable.h. HasteTracker adds rating
// buffs to the player's rating before converting to a percentage, then
// applies the percentage buffs multiplicatively on top.
//
// Values are estimates at item level 280. Real values scale with item level,
// but being off by 50-100 rating barely moves a GCD, whereas leaving out a
// 1200 rating proc makes casts appear to overlap.

namespace haste {

struct HasteRatingBuffInfo {
    int32_t rating;                // Haste rating gained (e.g., 1200 for a strong trinket)
    int32_t rating_per_stack;      // For stacking buffs: rating per stack
    bool stacks;                   // True if buff stacks affect rating additively

    constexpr HasteRatingBuffInfo(int32_t rating_value)
        : rating(rating_value), rating_per_stack(0), stacks(false) {}

    constexpr HasteRatingBuffInfo(int32_t base_rating, int32_t per_stack, bool is_stacking)
        : rating(base_rating), rating_per_stack(per_stack), stacks(is_stacking) {}
};

// To add a buff, take the spell id from a log and the rating at item level
// 280 from its spell description.
inline const std::unordered_map<uint32_t, HasteRatingBuffInfo>& getHasteRatingBuffTable() {
    static const std::unordered_map<uint32_t, HasteRatingBuffInfo> HASTE_RATING_BUFF_TABLE = {
        // Dragonflight Season 4 / The War Within Season 1
        { 1242504, HasteRatingBuffInfo(1200) },  // Hasty Hunt (Hunter set bonus - estimated ilvl 280)

        // Trinket procs
        { 1263318, HasteRatingBuffInfo(1000) },  // "The Heart of Wind has awoken"
        { 1259230, HasteRatingBuffInfo(900) },   // "Seized some poor soul's power"
        { 1246780, HasteRatingBuffInfo(800) },   // "Fallen into the rhythm of combat"
        { 1259633, HasteRatingBuffInfo(1100) },  // "Haste increased by $w1"
        { 1236994, HasteRatingBuffInfo(950) },   // "Granting highest secondary stat"
        { 1230153, HasteRatingBuffInfo(750) },   // Stacking buff
        { 274740,  HasteRatingBuffInfo(850) },   // "granting you their power" - racial
        { 1258887, HasteRatingBuffInfo(1050) },  // "Granted fleeting power by Amirdrassil"

        // Food, flask and potion haste is already part of the base rating
        // in COMBATANT_INFO, so only procs belong here.
    };

    return HASTE_RATING_BUFF_TABLE;
}

inline std::optional<HasteRatingBuffInfo> getHasteRating(uint32_t spell_id) {
    const auto& table = getHasteRatingBuffTable();
    auto it = table.find(spell_id);
    if (it != table.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace haste
