#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

// Percentage haste buffs: Bloodlust, Icy Veins, Berserking and the like.
//
// The entries are not written here. They come from data/haste_buffs_{version}.json,
// which tools/db2_extractor/build_haste_buffs.py generates from the game's spell
// data, and Database/HasteBuffData.h loads that file into this table at startup.
// Haste rating procs are a separate, smaller table in HasteRatingBuffTable.h.
//
// Haste is kept as permille throughout: 300 = 30%.

namespace haste {

struct HasteBuffInfo {
    int32_t haste_permille = 0;  // Haste one stack grants
    int32_t max_stacks = 1;      // 1 for a buff that does not stack

    bool stacks() const { return max_stacks > 1; }
};

class HasteBuffTable {
public:
    static HasteBuffTable& instance() {
        static HasteBuffTable table;
        return table;
    }

    void add(uint32_t spell_id, HasteBuffInfo info) { buffs_[spell_id] = info; }
    void clear() { buffs_.clear(); }

    std::optional<HasteBuffInfo> find(uint32_t spell_id) const {
        auto it = buffs_.find(spell_id);
        if (it != buffs_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    size_t size() const { return buffs_.size(); }
    bool empty() const { return buffs_.empty(); }

private:
    std::unordered_map<uint32_t, HasteBuffInfo> buffs_;
};

// Look up a haste buff by spell ID
inline std::optional<HasteBuffInfo> getHasteBuff(uint32_t spell_id) {
    return HasteBuffTable::instance().find(spell_id);
}

// Convert haste rating to percentage
// WoW formula: 660 haste rating = 1% haste at level 70+
// Returns haste as permille (300 = 30%)
constexpr int32_t hasteRatingToPermille(int32_t haste_rating) {
    // 660 rating = 1% = 10 permille
    // So: permille = rating * 10 / 660 = rating / 66
    return (haste_rating * 10) / 660;
}

// Multiplicative haste formula
// Haste stacks multiplicatively: new_haste = base * (1 + gain) + gain
// All values in permille (300 = 30%)
// Returns total haste in permille
constexpr int32_t addHasteMultiplicative(int32_t base_permille, int32_t gain_permille) {
    // Formula: new = base * (1 + gain/1000) + gain
    // = base + base*gain/1000 + gain
    // = base + gain + base*gain/1000
    return base_permille + gain_permille + (base_permille * gain_permille) / 1000;
}

// Remove haste buff (inverse of multiplicative addition)
// Returns new haste in permille after removing the buff
constexpr int32_t removeHasteMultiplicative(int32_t current_permille, int32_t buff_permille) {
    // Inverse of addHasteMultiplicative:
    // current = base + buff + base*buff/1000
    // current = base * (1 + buff/1000) + buff
    // base * (1 + buff/1000) = current - buff
    // base = (current - buff) * 1000 / (1000 + buff)
    return ((current_permille - buff_permille) * 1000) / (1000 + buff_permille);
}

} // namespace haste
