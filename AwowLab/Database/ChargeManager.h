#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct SpellCastEvent;  // Forward declaration

namespace awow {
    class TalentDatabase;
    class CooldownModifierRegistry;
    class SpellDataTable;
}

// Manages charge-based spell cooldowns (spells with multiple charges that recharge independently)
// Works with existing CooldownModifierRegistry for talent-granted additional charges
class ChargeManager {
public:
    ChargeManager() = default;

    // Load base charge data from JSON file
    // Format: { "charge_spells": [{ "spell_id": 22842, "base_charges": 2 }, ...] }
    bool loadFromJson(const std::string& filepath);

    // Set dependencies for talent-based charge additions
    void setTalentDatabase(const awow::TalentDatabase* talentDb) { talentDb_ = talentDb; }
    void setModifierRegistry(const awow::CooldownModifierRegistry* registry) { modifierRegistry_ = registry; }
    void setSpellDataTable(const awow::SpellDataTable* spellDataTable) { spellDataTable_ = spellDataTable; }

    // Query: Is this a charge-based spell in our config?
    bool isChargeSpell(uint32_t spellId) const;

    // Query: Should ChargeManager handle this spell's cooldown?
    // Returns true only if spell has >1 max charges AND valid recharge time
    // If false, fall back to normal cooldown system
    bool shouldHandleSpell(const std::string& playerGuid, uint32_t spellId) const;

    // Get max charges for a player (base + talent additions)
    uint8_t getMaxCharges(const std::string& playerGuid, uint32_t spellId) const;

    // Get available charges at current time based on cast history
    uint8_t getAvailableCharges(const std::string& playerGuid, uint32_t spellId,
                                 int32_t currentTimeMs,
                                 const std::vector<SpellCastEvent>& castHistory) const;

    // Get time until next charge is available (0 if any charge available)
    int32_t getTimeUntilNextCharge(const std::string& playerGuid, uint32_t spellId,
                                    int32_t currentTimeMs,
                                    const std::vector<SpellCastEvent>& castHistory) const;

    // Get recharge time for a charge spell (from SpellDataTable cooldown)
    int32_t getRechargeTimeMs(uint32_t spellId) const;

    size_t size() const { return chargeSpells_.size(); }

private:
    struct ChargeSpellData {
        uint32_t spell_id = 0;
        uint8_t base_charges = 1;
        int32_t recharge_ms_override = 0;  // 0 = use SpellDataTable, >0 = manual override
    };

    std::unordered_map<uint32_t, ChargeSpellData> chargeSpells_;
    const awow::TalentDatabase* talentDb_ = nullptr;
    const awow::CooldownModifierRegistry* modifierRegistry_ = nullptr;
    const awow::SpellDataTable* spellDataTable_ = nullptr;

    // Get talent-granted additional charges for a player/spell
    uint8_t getTalentAdditionalCharges(const std::string& playerGuid, uint32_t spellId) const;
};
