#pragma once
#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <unordered_set>
#include "structures.h"
#include "SpellCastEvent.h"

class SpellGCDDatabase;  // Forward declaration
class ChargeManager;     // Forward declaration
namespace awow { class CooldownReductionTable; }  // Forward declaration

// Database for efficient spell cast queries
class SpellCastDatabase {
private:
    // Map of actor GUID -> vector of spell casts (sorted by timestamp)
    std::map<std::string, std::vector<SpellCastEvent>> actor_casts_;
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;

    // Cooldown reduction table (optional - for resource-based CD reductions)
    const awow::CooldownReductionTable* reductionTable_ = nullptr;

    // Charge manager (optional - for charge-based spell tracking)
    const ChargeManager* chargeManager_ = nullptr;

public:
    // Load spell cast events
    void loadSpellCasts(const std::vector<SpellCastEvent>& casts) {
        actor_casts_.clear();

        if (casts.empty()) {
            min_timestamp_ = 0;
            max_timestamp_ = 0;
            return;
        }

        // Group by actor and sort by timestamp
        for (const auto& cast : casts) {
            actor_casts_[cast.actor_guid].push_back(cast);
        }

        // Sort each actor's casts by end timestamp
        for (auto& [guid, castList] : actor_casts_) {
            std::sort(castList.begin(), castList.end(),
                     [](const SpellCastEvent& a, const SpellCastEvent& b) {
                         return a.end_timestamp_ms < b.end_timestamp_ms;
                     });
        }

        min_timestamp_ = casts[0].end_timestamp_ms;
        max_timestamp_ = casts[0].end_timestamp_ms;
        for (const auto& cast : casts) {
            if (cast.end_timestamp_ms < min_timestamp_) min_timestamp_ = cast.end_timestamp_ms;
            if (cast.end_timestamp_ms > max_timestamp_) max_timestamp_ = cast.end_timestamp_ms;
        }
    }

    // Earliest completed cast of a spell across every actor, or nullopt
    // when nobody cast it. Phase tracking uses this to place phase
    // boundaries ("phase 2 starts at the first cast of X").
    std::optional<int32_t> firstCastTime(uint32_t spell_id) const {
        std::optional<int32_t> earliest;
        for (const auto& [guid, casts] : actor_casts_) {
            for (const auto& cast : casts) {
                if (cast.spell_id != spell_id || cast.cancelled) continue;
                int32_t castTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms
                                                               : cast.end_timestamp_ms;
                if (!earliest || castTime < *earliest) {
                    earliest = castTime;
                }
            }
        }
        return earliest;
    }

    // Get spell casts for a specific actor within a time window
    std::vector<SpellCastEvent> getCastsForActor(const std::string& actor_guid,
                                                   int32_t current_time_ms,
                                                   int32_t window_ms) const {
        std::vector<SpellCastEvent> result;

        auto it = actor_casts_.find(actor_guid);
        if (it == actor_casts_.end()) {
            return result;
        }

        const auto& casts = it->second;

        // Calculate time range (signed arithmetic handles negative timestamps naturally)
        int32_t start_time = current_time_ms - window_ms;
        int32_t end_time = current_time_ms;

        // Find casts in time range by when the cast started, not when it completed
        // This ensures instant spells and in-progress casts are visible immediately
        for (const auto& cast : casts) {
            // Use start_timestamp if available, otherwise use end_timestamp (for instant spells)
            int32_t castStartTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;

            // Include cast if it started within the window AND started before or at current time
            if (castStartTime >= start_time && castStartTime <= end_time) {
                result.push_back(cast);
            }
        }

        return result;
    }

    // Get recent N casts for an actor (up to current time)
    std::vector<SpellCastEvent> getRecentCastsForActor(const std::string& actor_guid,
                                                         int32_t current_time_ms,
                                                         size_t count = 5) const {
        std::vector<SpellCastEvent> result;

        auto it = actor_casts_.find(actor_guid);
        if (it == actor_casts_.end()) {
            return result;
        }

        const auto& casts = it->second;

        // Find all casts that have started by current time
        for (const auto& cast : casts) {
            int32_t castStartTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;
            if (castStartTime <= current_time_ms) {
                result.push_back(cast);
            }
        }

        // Keep only the last N casts
        if (result.size() > count) {
            result.erase(result.begin(), result.end() - count);
        }

        return result;
    }

    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }
    bool empty() const { return actor_casts_.empty(); }
    size_t size() const {
        size_t total = 0;
        for (const auto& [guid, casts] : actor_casts_) {
            total += casts.size();
        }
        return total;
    }

    // Get all unique spell IDs in the database (for pre-loading icons)
    std::vector<uint32_t> getUniqueSpellIDs() const {
        std::unordered_set<uint32_t> uniqueSpells;
        for (const auto& [guid, castList] : actor_casts_) {
            for (const auto& cast : castList) {
                uniqueSpells.insert(cast.spell_id);
            }
        }
        return std::vector<uint32_t>(uniqueSpells.begin(), uniqueSpells.end());
    }

    // Get all actor casts (for iteration)
    const std::map<std::string, std::vector<SpellCastEvent>>& getAllActorCasts() const {
        return actor_casts_;
    }

    // Get mutable actor casts (for cooldown population)
    std::map<std::string, std::vector<SpellCastEvent>>& getMutableActorCasts() {
        return actor_casts_;
    }

    // Get the most recent cast of a specific spell by an actor before a given time
    // Returns nullptr if no matching cast found
    const SpellCastEvent* getLastCastForSpell(const std::string& actorGuid,
                                               uint32_t spellId,
                                               int32_t beforeTimeMs) const {
        auto it = actor_casts_.find(actorGuid);
        if (it == actor_casts_.end()) return nullptr;

        const SpellCastEvent* lastCast = nullptr;
        for (const auto& cast : it->second) {
            if (cast.spell_id == spellId && cast.end_timestamp_ms <= beforeTimeMs) {
                lastCast = &cast;  // Keep updating to get the latest one
            }
        }
        return lastCast;
    }

    // Set the cooldown reduction table (for resource-based CD reductions)
    void setReductionTable(const awow::CooldownReductionTable* table) {
        reductionTable_ = table;
    }

    // Set the charge manager (for charge-based spell tracking)
    void setChargeManager(const ChargeManager* manager) {
        chargeManager_ = manager;
    }

    // Check if a spell is on cooldown at a given time, accounting for reductions
    // Returns true if spell is still on cooldown after applying all reductions
    bool isSpellOnCooldown(const std::string& actorGuid,
                          uint32_t spellId,
                          int32_t currentTimeMs) const;

    // Get remaining cooldown for a spell, accounting for reductions
    // Returns 0 if spell is ready
    int32_t getRemainingCooldownWithReductions(const std::string& actorGuid,
                                               uint32_t spellId,
                                               int32_t currentTimeMs) const;
};
