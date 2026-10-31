#pragma once
#include <string>
#include <vector>
#include <cstdint>

class SpellCastDatabase;
class AuraDatabase;

// Unified spell state for all UI modules
enum class SpellState : uint8_t {
    Ready,      // Off cooldown, not active (gold border)
    OnCooldown, // Used, on CD (gray icon + timer)
    Active      // Aura currently applied (green glow)
};

// Result of spell state computation
struct SpellStateResult {
    uint32_t spellId = 0;
    SpellState state = SpellState::Ready;
    uint32_t remainingMs = 0;  // CD remaining or aura duration remaining
    uint8_t stacks = 0;        // Aura stack count (0 if N/A)
};

// Centralized spell state computation for all UI modules
// Checks both AuraDatabase (for active buffs) and SpellCastDatabase (for cooldowns)
// Priority: Active > OnCooldown > Ready
class SpellStateComputer {
public:
    // Compute state for a single spell on a player
    // Checks if aura is active first, then falls back to cooldown check
    static SpellStateResult computeSpellState(
        const std::string& playerGuid,
        uint32_t spellId,
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        uint32_t currentTimeMs);

    // Compute states for multiple spells (batch query, more efficient)
    // Fetches active auras once, then checks each spell
    static std::vector<SpellStateResult> computeSpellStates(
        const std::string& playerGuid,
        const std::vector<uint32_t>& spellIds,
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        uint32_t currentTimeMs);
};
