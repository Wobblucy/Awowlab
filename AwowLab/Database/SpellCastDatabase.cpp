#include "SpellCastDatabase.h"
#include "CooldownReductionTable.h"
#include "ChargeManager.h"
#include "Core/StringInterner.h"

bool SpellCastDatabase::isSpellOnCooldown(const std::string& actorGuid,
                                          uint32_t spellId,
                                          int32_t currentTimeMs) const {
    // Check if ChargeManager should handle this spell (>1 charge AND valid recharge time)
    if (chargeManager_ && chargeManager_->shouldHandleSpell(actorGuid, spellId)) {
        auto it = actor_casts_.find(actorGuid);
        if (it == actor_casts_.end()) {
            return false;  // No casts, all charges available
        }
        // Spell is on cooldown if no charges are available
        return chargeManager_->getAvailableCharges(actorGuid, spellId, currentTimeMs, it->second) == 0;
    }

    const SpellCastEvent* lastCast = getLastCastForSpell(actorGuid, spellId, currentTimeMs);
    if (!lastCast) {
        return false;  // Never cast, not on cooldown
    }

    if (lastCast->cooldown_ms <= 0) {
        return false;  // No cooldown
    }

    int32_t baseReadyTime = lastCast->cooldown_ready_ms;

    // Apply cooldown reductions if reduction table is available
    if (reductionTable_) {
        // Get total reductions between last cast and current time
        uint32_t actorGuidId = guidInterner().intern(actorGuid);
        int32_t totalReduction = reductionTable_->getTotalReduction(
            actorGuidId,
            spellId,
            lastCast->end_timestamp_ms,  // Start: when spell was cast
            currentTimeMs                // End: current time
        );

        // Apply reductions
        baseReadyTime -= totalReduction;
    }

    return currentTimeMs < baseReadyTime;
}

int32_t SpellCastDatabase::getRemainingCooldownWithReductions(const std::string& actorGuid,
                                                               uint32_t spellId,
                                                               int32_t currentTimeMs) const {
    // Check if ChargeManager should handle this spell (>1 charge AND valid recharge time)
    if (chargeManager_ && chargeManager_->shouldHandleSpell(actorGuid, spellId)) {
        auto it = actor_casts_.find(actorGuid);
        if (it == actor_casts_.end()) {
            return 0;  // No casts, all charges available
        }
        return chargeManager_->getTimeUntilNextCharge(actorGuid, spellId, currentTimeMs, it->second);
    }

    const SpellCastEvent* lastCast = getLastCastForSpell(actorGuid, spellId, currentTimeMs);
    if (!lastCast) {
        return 0;  // Never cast, no cooldown
    }

    if (lastCast->cooldown_ms <= 0) {
        return 0;  // No cooldown
    }

    int32_t baseReadyTime = lastCast->cooldown_ready_ms;

    // Apply cooldown reductions if reduction table is available
    if (reductionTable_) {
        // Get total reductions between last cast and current time
        uint32_t actorGuidId = guidInterner().intern(actorGuid);
        int32_t totalReduction = reductionTable_->getTotalReduction(
            actorGuidId,
            spellId,
            lastCast->end_timestamp_ms,  // Start: when spell was cast
            currentTimeMs                // End: current time
        );

        // Apply reductions
        baseReadyTime -= totalReduction;
    }

    if (currentTimeMs >= baseReadyTime) {
        return 0;  // Ready
    }

    return baseReadyTime - currentTimeMs;
}
