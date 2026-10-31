#include "ChargeManager.h"
#include "SpellCastDatabase.h"
#include "SpellDataTable.h"
#include "TalentDatabase.h"
#include "CooldownModifiers/CooldownModifierRegistry.h"
#include "CooldownModifiers/CooldownModifierTypes.h"
#include "Core/ErrorLogger.h"
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <algorithm>

bool ChargeManager::loadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        AWOW_LOG_INFO("DATA", "ChargeManager: could not open " + filepath);
        return false;
    }

    rapidjson::IStreamWrapper isw(file);
    rapidjson::Document doc;
    doc.ParseStream(isw);

    if (doc.HasParseError() || !doc.IsObject()) {
        AWOW_LOG_INFO("DATA", "ChargeManager: JSON parse error in " + filepath);
        return false;
    }

    chargeSpells_.clear();

    if (doc.HasMember("charge_spells") && doc["charge_spells"].IsArray()) {
        const auto& arr = doc["charge_spells"];
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
            const auto& entry = arr[i];

            if (!entry.HasMember("spell_id") || !entry.HasMember("base_charges")) {
                continue;  // Skip incomplete entries
            }

            ChargeSpellData data;
            data.spell_id = entry["spell_id"].GetUint();
            data.base_charges = static_cast<uint8_t>(entry["base_charges"].GetUint());

            // Optional recharge_ms override for spells missing from SpellDataTable
            if (entry.HasMember("recharge_ms") && entry["recharge_ms"].IsInt()) {
                data.recharge_ms_override = entry["recharge_ms"].GetInt();
            }

            chargeSpells_[data.spell_id] = data;
        }
    }

    return true;
}

bool ChargeManager::isChargeSpell(uint32_t spellId) const {
    // Check JSON config first (manual overrides)
    if (chargeSpells_.find(spellId) != chargeSpells_.end()) {
        return true;
    }

    // Then check SpellDataTable for category-based charges
    if (spellDataTable_ && spellDataTable_->getBaseCharges(spellId) > 0) {
        return true;
    }

    return false;
}

bool ChargeManager::shouldHandleSpell(const std::string& playerGuid, uint32_t spellId) const {
    if (!isChargeSpell(spellId)) {
        return false;
    }

    uint8_t maxCharges = getMaxCharges(playerGuid, spellId);
    int32_t rechargeMs = getRechargeTimeMs(spellId);

    // Only handle if we have multiple charges AND valid recharge time
    // Otherwise fall back to normal cooldown system
    return (maxCharges > 1 && rechargeMs > 0);
}

uint8_t ChargeManager::getMaxCharges(const std::string& playerGuid, uint32_t spellId) const {
    // Try JSON config first (manual overrides)
    uint8_t baseCharges = 0;
    auto it = chargeSpells_.find(spellId);
    if (it != chargeSpells_.end()) {
        baseCharges = it->second.base_charges;
    } else if (spellDataTable_) {
        // Fall back to SpellDataTable (category-based charges from DB2)
        baseCharges = spellDataTable_->getBaseCharges(spellId);
    }

    if (baseCharges == 0) {
        return 0;  // Not a charge spell
    }

    uint8_t talentCharges = getTalentAdditionalCharges(playerGuid, spellId);

    return baseCharges + talentCharges;
}

uint8_t ChargeManager::getTalentAdditionalCharges(const std::string& playerGuid, uint32_t spellId) const {
    if (!talentDb_ || !modifierRegistry_) {
        return 0;
    }

    const auto* playerTalents = talentDb_->getPlayerTalentSpells(playerGuid);
    if (!playerTalents) {
        return 0;
    }

    auto affectingTalents = modifierRegistry_->getTalentsAffectingSpell(spellId);

    uint8_t additionalCharges = 0;
    for (uint32_t talentSpellId : affectingTalents) {
        if (playerTalents->count(talentSpellId) == 0) {
            continue;
        }

        const auto* modifiers = modifierRegistry_->getModifiersForTalent(talentSpellId);
        if (!modifiers) {
            continue;
        }

        for (const auto& mod : *modifiers) {
            if (mod.modified_spell_id != spellId) {
                continue;
            }

            if (mod.modifier_type == awow::ModifierType::ChargeBased) {
                additionalCharges += mod.charge.additional_charges;
            }
        }
    }

    return additionalCharges;
}

int32_t ChargeManager::getRechargeTimeMs(uint32_t spellId) const {
    auto it = chargeSpells_.find(spellId);
    if (it != chargeSpells_.end() && it->second.recharge_ms_override > 0) {
        // Use manual override if specified
        return it->second.recharge_ms_override;
    }

    if (!spellDataTable_) {
        return 0;
    }

    // Recharge time is the spell's cooldown from SpellDataTable
    return spellDataTable_->getBaseCooldown(spellId);
}

uint8_t ChargeManager::getAvailableCharges(const std::string& playerGuid, uint32_t spellId,
                                            int32_t currentTimeMs,
                                            const std::vector<SpellCastEvent>& castHistory) const {
    // Check if this is a charge spell (JSON config or SpellDataTable)
    if (!isChargeSpell(spellId)) {
        return 0;  // Not a charge spell
    }

    uint8_t maxCharges = getMaxCharges(playerGuid, spellId);
    int32_t rechargeMs = getRechargeTimeMs(spellId);

    // If only 1 charge or no recharge time, don't handle as charge spell
    // Let the normal cooldown system handle it
    if (maxCharges <= 1 || rechargeMs <= 0) {
        return maxCharges;  // Signal to fall back to normal system
    }

    // Count charges that are still on cooldown (haven't fully recharged)
    uint8_t chargesOnCooldown = 0;
    for (const auto& cast : castHistory) {
        if (cast.spell_id != spellId) {
            continue;
        }

        int32_t timeSinceCast = currentTimeMs - cast.end_timestamp_ms;
        if (timeSinceCast >= 0 && timeSinceCast < rechargeMs) {
            // This cast's charge hasn't fully recharged yet
            chargesOnCooldown++;
        }
    }

    // Available = max - on cooldown (but never negative)
    if (chargesOnCooldown >= maxCharges) {
        return 0;
    }
    return maxCharges - chargesOnCooldown;
}

int32_t ChargeManager::getTimeUntilNextCharge(const std::string& playerGuid, uint32_t spellId,
                                               int32_t currentTimeMs,
                                               const std::vector<SpellCastEvent>& castHistory) const {
    // Check if this is a charge spell (JSON config or SpellDataTable)
    if (!isChargeSpell(spellId)) {
        return 0;  // Not a charge spell
    }

    uint8_t available = getAvailableCharges(playerGuid, spellId, currentTimeMs, castHistory);
    if (available > 0) {
        return 0;  // At least one charge is available
    }

    int32_t rechargeMs = getRechargeTimeMs(spellId);
    if (rechargeMs <= 0) {
        return 0;  // No cooldown
    }

    // Find the cast that will recharge soonest
    int32_t earliestReadyTime = INT32_MAX;
    for (const auto& cast : castHistory) {
        if (cast.spell_id != spellId) {
            continue;
        }

        int32_t readyTime = cast.end_timestamp_ms + rechargeMs;
        if (readyTime > currentTimeMs && readyTime < earliestReadyTime) {
            earliestReadyTime = readyTime;
        }
    }

    if (earliestReadyTime == INT32_MAX) {
        return 0;  // No casts found
    }

    return earliestReadyTime - currentTimeMs;
}
