#include "CooldownReductionTable.h"
#include <algorithm>

namespace awow {

    void CooldownReductionTable::loadReductions(const std::vector<CooldownReductionRecord>& reductions) {
        clear();

        // Group reductions by actor and spell, then sort by timestamp
        for (const auto& record : reductions) {
            reductions_[record.actor_guid_id][record.spell_id].push_back(record);
        }

        // Sort each vector by timestamp for binary search
        for (auto& [actor_guid_id, spell_map] : reductions_) {
            for (auto& [spell_id, records] : spell_map) {
                std::sort(records.begin(), records.end(),
                    [](const CooldownReductionRecord& a, const CooldownReductionRecord& b) {
                        return a.timestamp_ms < b.timestamp_ms;
                    });
                total_records_ += records.size();
            }
        }
    }

    int32_t CooldownReductionTable::getTotalReduction(
        uint32_t actor_guid_id,
        uint32_t spell_id,
        int32_t start_timestamp_ms,
        int32_t end_timestamp_ms
    ) const {
        auto actor_it = reductions_.find(actor_guid_id);
        if (actor_it == reductions_.end()) {
            return 0;
        }

        auto spell_it = actor_it->second.find(spell_id);
        if (spell_it == actor_it->second.end()) {
            return 0;
        }

        const auto& records = spell_it->second;

        // Binary search for start of time window
        auto start_it = std::lower_bound(records.begin(), records.end(), start_timestamp_ms,
            [](const CooldownReductionRecord& record, int32_t timestamp) {
                return record.timestamp_ms < timestamp;
            });

        // Binary search for end of time window
        auto end_it = std::upper_bound(records.begin(), records.end(), end_timestamp_ms,
            [](int32_t timestamp, const CooldownReductionRecord& record) {
                return timestamp < record.timestamp_ms;
            });

        // Sum all reductions in the window
        int32_t total = 0;
        for (auto it = start_it; it != end_it; ++it) {
            total += it->reduction_ms;
        }

        return total;
    }

    void CooldownReductionTable::clear() {
        reductions_.clear();
        total_records_ = 0;
    }

    size_t CooldownReductionTable::recordCount() const {
        return total_records_;
    }

} // namespace awow
