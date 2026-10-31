#include "Core/SpellStateComputer.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/AuraDatabase.h"

SpellStateResult SpellStateComputer::computeSpellState(
    const std::string& playerGuid,
    uint32_t spellId,
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    uint32_t currentTimeMs)
{
    SpellStateResult result;
    result.spellId = spellId;
    result.state = SpellState::Ready;
    result.remainingMs = 0;
    result.stacks = 0;

    // An active aura takes priority over cooldown state
    if (auraDb) {
        auto activeAuras = auraDb->getActiveAurasOnTarget(playerGuid, currentTimeMs);
        for (const auto* aura : activeAuras) {
            if (aura->spell_id == spellId) {
                result.state = SpellState::Active;
                result.stacks = aura->current_stacks;
                if (aura->removed_at_ms != UINT32_MAX && static_cast<int32_t>(aura->removed_at_ms) > static_cast<int32_t>(currentTimeMs)) {
                    result.remainingMs = aura->removed_at_ms - currentTimeMs;
                }
                return result;
            }
        }
    }

    // Remaining cooldown includes resource-based reductions
    if (spellCastDb) {
        if (spellCastDb->isSpellOnCooldown(playerGuid, spellId, currentTimeMs)) {
            result.state = SpellState::OnCooldown;
            result.remainingMs = spellCastDb->getRemainingCooldownWithReductions(playerGuid, spellId, currentTimeMs);
            return result;
        }
    }

    return result;
}

std::vector<SpellStateResult> SpellStateComputer::computeSpellStates(
    const std::string& playerGuid,
    const std::vector<uint32_t>& spellIds,
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    uint32_t currentTimeMs)
{
    std::vector<SpellStateResult> results;
    results.reserve(spellIds.size());

    // Fetch active auras once for the whole batch
    std::vector<const AuraInstance*> activeAuras;
    if (auraDb) {
        activeAuras = auraDb->getActiveAurasOnTarget(playerGuid, currentTimeMs);
    }

    for (uint32_t spellId : spellIds) {
        SpellStateResult result;
        result.spellId = spellId;
        result.state = SpellState::Ready;
        result.remainingMs = 0;
        result.stacks = 0;

        bool isActive = false;
        for (const auto* aura : activeAuras) {
            if (aura->spell_id == spellId) {
                result.state = SpellState::Active;
                result.stacks = aura->current_stacks;
                if (aura->removed_at_ms != UINT32_MAX && static_cast<int32_t>(aura->removed_at_ms) > static_cast<int32_t>(currentTimeMs)) {
                    result.remainingMs = aura->removed_at_ms - currentTimeMs;
                }
                isActive = true;
                break;
            }
        }

        if (!isActive && spellCastDb) {
            if (spellCastDb->isSpellOnCooldown(playerGuid, spellId, currentTimeMs)) {
                result.state = SpellState::OnCooldown;
                result.remainingMs = spellCastDb->getRemainingCooldownWithReductions(playerGuid, spellId, currentTimeMs);
            }
        }

        results.push_back(result);
    }

    return results;
}
