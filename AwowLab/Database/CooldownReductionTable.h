#pragma once
#include "Structures/Records.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace awow {

    // Table for tracking resource-based cooldown reductions
    // Stores lightweight records of when spell cooldowns were reduced
    // Used for queries like "how much CD reduction since last cast?"
    class CooldownReductionTable {
    public:
        // Load reductions from OutputWriter results
        // Records should be pre-sorted by timestamp for binary search
        void loadReductions(const std::vector<CooldownReductionRecord>& reductions);

        // Get total reductions for a spell between two timestamps
        // Use case: sum all reductions since last cast
        // Returns total milliseconds of CD reduction in the time window
        int32_t getTotalReduction(
            uint32_t actor_guid_id,
            uint32_t spell_id,
            int32_t start_timestamp_ms,
            int32_t end_timestamp_ms
        ) const;

        void clear();

        // Get total number of reduction records loaded
        size_t recordCount() const;

    private:
        // Per-actor, per-spell sorted vectors for binary search
        // Structure: actor_guid_id -> spell_id -> sorted vector of reductions
        std::unordered_map<uint32_t,
            std::unordered_map<uint32_t, std::vector<CooldownReductionRecord>>> reductions_;

        // Total record count (for debugging)
        size_t total_records_ = 0;
    };

} // namespace awow
