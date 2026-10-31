#include "../CombatDatabase.h"
#include "Core/StringInterner.h"
#include <algorithm>
#include <unordered_map>

std::vector<DamageTimeBucket> CombatDatabase::getActorDamageTimeSeries(
    const std::string& actor_guid,
    uint32_t bucket_size_ms,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (!actorMap_ || actorMap_->empty() || bucket_size_ms == 0) {
        return {};
    }

    // Results with a blacklist are not cached, because the blacklist can change
    std::string cacheKey = actor_guid + "_" + std::to_string(bucket_size_ms);
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        auto cacheIt = timeSeriesCache_.find(cacheKey);
        if (cacheIt != timeSeriesCache_.end()) {
            return cacheIt->second;
        }
    }

    std::vector<DamageTimeBucket> result;

    // find() never adds to the pool; an unknown guid means no data.
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) {
        return result;
    }

    std::vector<const CombatRecord*> allRecords;

    auto actor_it = actorMap_->find(actor_id);
    if (actor_it != actorMap_->end()) {
        for (const auto& record : actor_it->second.damage_dealt_table) {
            // Friendly fire counts as damage taken, not damage done
            if (record.flags & CombatEventFlags::FriendlyFire) continue;
            allRecords.push_back(&record);
        }
    }

    auto petIt = ownerToPetsMap_.find(actor_id);
    if (petIt != ownerToPetsMap_.end()) {
        for (StringInterner::Id pet_id : petIt->second) {
            auto pet_it = actorMap_->find(pet_id);
            if (pet_it != actorMap_->end()) {
                for (const auto& record : pet_it->second.damage_dealt_table) {
                    if (record.flags & CombatEventFlags::FriendlyFire) continue;
                    allRecords.push_back(&record);
                }
            }
        }
    }

    if (allRecords.empty()) {
        return result;
    }

    std::sort(allRecords.begin(), allRecords.end(),
        [](const CombatRecord* a, const CombatRecord* b) {
            return a->timestamp_ms < b->timestamp_ms;
        });

    uint32_t numBuckets = (max_timestamp_ - min_timestamp_ + bucket_size_ms - 1) / bucket_size_ms;
    if (numBuckets == 0) numBuckets = 1;
    if (numBuckets > 10000) numBuckets = 10000;  // Cap for very long logs

    result.resize(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        result[i].timestamp_ms = min_timestamp_ + i * bucket_size_ms;
    }

    // Each event is spread 25/50/25 over its bucket and the two neighbours,
    // which smooths the chart
    std::vector<std::unordered_map<uint32_t, int64_t>> bucketSpellMaps(numBuckets);

    for (const auto* record : allRecords) {
        if (record->timestamp_ms < min_timestamp_) continue;

        if (blacklistedSpells && blacklistedSpells->count(record->spell_id) > 0) {
            continue;
        }

        uint32_t centerIndex = (record->timestamp_ms - min_timestamp_) / bucket_size_ms;
        if (centerIndex >= numBuckets) centerIndex = numBuckets - 1;

        // Mob weighting applies here too so the chart matches the meter
        int64_t amount = applyWeight(record->amount, targetWeight(record->target_guid_id));
        int64_t centerAmount = amount / 2;
        int64_t sideAmount = amount / 4;
        int64_t remainder = amount - centerAmount - 2 * sideAmount;
        centerAmount += remainder;  // rounding goes to the centre bucket

        result[centerIndex].total_damage += centerAmount;
        bucketSpellMaps[centerIndex][record->spell_id] += centerAmount;

        if (centerIndex > 0) {
            result[centerIndex - 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex - 1][record->spell_id] += sideAmount;
        }

        if (centerIndex + 1 < numBuckets) {
            result[centerIndex + 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex + 1][record->spell_id] += sideAmount;
        }
    }

    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto& bucket = result[i];
        auto& spellMap = bucketSpellMaps[i];

        bucket.by_spell.reserve(spellMap.size());
        for (const auto& [spell_id, damage] : spellMap) {
            bucket.by_spell.emplace_back(spell_id, damage);
        }

        std::sort(bucket.by_spell.begin(), bucket.by_spell.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    if (!blacklistedSpells || blacklistedSpells->empty()) {
        timeSeriesCache_[cacheKey] = result;
    }

    return result;
}

std::vector<DamageTimeBucket> CombatDatabase::getActorHealingTimeSeries(
    const std::string& actor_guid,
    uint32_t bucket_size_ms,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (!actorMap_ || actorMap_->empty() || bucket_size_ms == 0) {
        return {};
    }

    std::string cacheKey = actor_guid + "_heal_" + std::to_string(bucket_size_ms);
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        auto cacheIt = timeSeriesCache_.find(cacheKey);
        if (cacheIt != timeSeriesCache_.end()) {
            return cacheIt->second;
        }
    }

    std::vector<DamageTimeBucket> result;

    // find() never adds to the pool; an unknown guid means no data.
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) {
        return result;
    }

    std::vector<const CombatRecord*> allRecords;

    auto actor_it = actorMap_->find(actor_id);
    if (actor_it != actorMap_->end()) {
        for (const auto& record : actor_it->second.healing_done_table) {
            allRecords.push_back(&record);
        }
    }

    auto petIt = ownerToPetsMap_.find(actor_id);
    if (petIt != ownerToPetsMap_.end()) {
        for (StringInterner::Id pet_id : petIt->second) {
            auto pet_it = actorMap_->find(pet_id);
            if (pet_it != actorMap_->end()) {
                for (const auto& record : pet_it->second.healing_done_table) {
                    allRecords.push_back(&record);
                }
            }
        }
    }

    if (allRecords.empty()) {
        return result;
    }

    std::sort(allRecords.begin(), allRecords.end(),
        [](const CombatRecord* a, const CombatRecord* b) {
            return a->timestamp_ms < b->timestamp_ms;
        });

    uint32_t numBuckets = (max_timestamp_ - min_timestamp_ + bucket_size_ms - 1) / bucket_size_ms;
    if (numBuckets == 0) numBuckets = 1;
    if (numBuckets > 10000) numBuckets = 10000;  // Cap for very long logs

    result.resize(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        result[i].timestamp_ms = min_timestamp_ + i * bucket_size_ms;
    }

    std::vector<std::unordered_map<uint32_t, int64_t>> bucketSpellMaps(numBuckets);

    for (const auto* record : allRecords) {
        if (record->timestamp_ms < min_timestamp_) continue;

        if (blacklistedSpells && blacklistedSpells->count(record->spell_id) > 0) {
            continue;
        }

        uint32_t centerIndex = (record->timestamp_ms - min_timestamp_) / bucket_size_ms;
        if (centerIndex >= numBuckets) centerIndex = numBuckets - 1;

        int64_t amount = record->amount;
        int64_t centerAmount = amount / 2;
        int64_t sideAmount = amount / 4;
        int64_t remainder = amount - centerAmount - 2 * sideAmount;
        centerAmount += remainder;  // rounding goes to the centre bucket

        result[centerIndex].total_damage += centerAmount;
        bucketSpellMaps[centerIndex][record->spell_id] += centerAmount;

        if (centerIndex > 0) {
            result[centerIndex - 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex - 1][record->spell_id] += sideAmount;
        }

        if (centerIndex + 1 < numBuckets) {
            result[centerIndex + 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex + 1][record->spell_id] += sideAmount;
        }
    }

    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto& bucket = result[i];
        auto& spellMap = bucketSpellMaps[i];

        bucket.by_spell.reserve(spellMap.size());
        for (const auto& [spell_id, amount] : spellMap) {
            bucket.by_spell.emplace_back(spell_id, amount);
        }

        std::sort(bucket.by_spell.begin(), bucket.by_spell.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    if (!blacklistedSpells || blacklistedSpells->empty()) {
        timeSeriesCache_[cacheKey] = result;
    }

    return result;
}

std::vector<DamageTimeBucket> CombatDatabase::getActorDamageTakenTimeSeries(
    const std::string& actor_guid,
    uint32_t bucket_size_ms,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (damageTakenIndex_.empty() || bucket_size_ms == 0) {
        return {};
    }

    std::string cacheKey = actor_guid + "_taken_" + std::to_string(bucket_size_ms);
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        auto cacheIt = timeSeriesCache_.find(cacheKey);
        if (cacheIt != timeSeriesCache_.end()) {
            return cacheIt->second;
        }
    }

    std::vector<DamageTimeBucket> result;

    // find() never adds to the pool; an unknown guid means no damage taken.
    StringInterner::Id actor_guid_id = guidInterner().find(actor_guid);
    if (actor_guid_id == StringInterner::INVALID) {
        return result;
    }

    auto damageIt = damageTakenIndex_.find(actor_guid_id);
    if (damageIt == damageTakenIndex_.end() || damageIt->second.empty()) {
        return result;
    }

    const auto& allRecords = damageIt->second;

    uint32_t numBuckets = (max_timestamp_ - min_timestamp_ + bucket_size_ms - 1) / bucket_size_ms;
    if (numBuckets == 0) numBuckets = 1;
    if (numBuckets > 10000) numBuckets = 10000;  // Cap for very long logs

    result.resize(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        result[i].timestamp_ms = min_timestamp_ + i * bucket_size_ms;
    }

    std::vector<std::unordered_map<uint32_t, int64_t>> bucketSpellMaps(numBuckets);

    for (const auto* record : allRecords) {
        if (record->timestamp_ms < min_timestamp_) continue;

        if (blacklistedSpells && blacklistedSpells->count(record->spell_id) > 0) {
            continue;
        }

        uint32_t centerIndex = (record->timestamp_ms - min_timestamp_) / bucket_size_ms;
        if (centerIndex >= numBuckets) centerIndex = numBuckets - 1;

        int64_t amount = record->amount;
        int64_t centerAmount = amount / 2;
        int64_t sideAmount = amount / 4;
        int64_t remainder = amount - centerAmount - 2 * sideAmount;
        centerAmount += remainder;  // rounding goes to the centre bucket

        result[centerIndex].total_damage += centerAmount;
        bucketSpellMaps[centerIndex][record->spell_id] += centerAmount;

        if (centerIndex > 0) {
            result[centerIndex - 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex - 1][record->spell_id] += sideAmount;
        }

        if (centerIndex + 1 < numBuckets) {
            result[centerIndex + 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex + 1][record->spell_id] += sideAmount;
        }
    }

    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto& bucket = result[i];
        auto& spellMap = bucketSpellMaps[i];

        bucket.by_spell.reserve(spellMap.size());
        for (const auto& [spell_id, damage] : spellMap) {
            bucket.by_spell.emplace_back(spell_id, damage);
        }

        std::sort(bucket.by_spell.begin(), bucket.by_spell.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    if (!blacklistedSpells || blacklistedSpells->empty()) {
        timeSeriesCache_[cacheKey] = result;
    }

    return result;
}
