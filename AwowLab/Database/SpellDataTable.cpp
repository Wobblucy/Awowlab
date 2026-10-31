#include "SpellDataTable.h"
#include "Core/ErrorLogger.h"
#include <fstream>
#include <iostream>

namespace awow {

    // Binary format: see build_cooldown_tables.py
    // Header: [uint32_t count]
    // Records: [SpellDataRecord...] sorted by spell_id
    //
    // struct SpellDataRecord {
    //     uint32_t spell_id;
    //     int32_t  gcd_ms;
    //     int32_t  cooldown_ms;
    //     int32_t  cast_time_ms;
    //     uint8_t  gcd_haste_affected;
    //     uint8_t  cd_haste_affected;
    //     uint8_t  base_charges;    // From SpellCategory table (0 = not charge-based)
    //     uint8_t  padding[1];
    // }; // 20 bytes

    #pragma pack(push, 1)
    struct SpellDataRecord {
        uint32_t spell_id;
        int32_t  gcd_ms;
        int32_t  cooldown_ms;
        int32_t  cast_time_ms;
        uint8_t  gcd_haste_affected;
        uint8_t  cd_haste_affected;
        uint8_t  base_charges;
        uint8_t  padding[1];
    };
    #pragma pack(pop)

    bool SpellDataTable::loadFromBinary(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            AWOW_LOG_ERROR("DATA", "SpellDataTable: Failed to open binary file: " + filepath);
            return false;
        }

        // Read header
        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));

        if (!file) {
            AWOW_LOG_ERROR("DATA", "SpellDataTable: Failed to read header from: " + filepath);
            return false;
        }

        // Read all records
        for (uint32_t i = 0; i < count; ++i) {
            SpellDataRecord record;
            file.read(reinterpret_cast<char*>(&record), sizeof(SpellDataRecord));

            if (!file) {
                AWOW_LOG_ERROR("DATA", "SpellDataTable: Failed to read record " + std::to_string(i) + " from: " + filepath);
                break;
            }

            SpellInfo info;
            info.spell_id = record.spell_id;
            info.gcd_ms = record.gcd_ms;
            info.cooldown_ms = record.cooldown_ms;
            info.cast_time_ms = record.cast_time_ms;
            info.gcd_haste_affected = (record.gcd_haste_affected != 0);
            info.cd_haste_affected = (record.cd_haste_affected != 0);
            info.base_charges = record.base_charges;

            spells_[info.spell_id] = info;
        }

        return true;
    }

    bool SpellDataTable::loadRelevantSpells(const std::string& filepath, const std::unordered_set<uint32_t>& spell_ids) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            AWOW_LOG_ERROR("DATA", "SpellDataTable: Failed to open binary file: " + filepath);
            return false;
        }

        // Read header
        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));

        if (!file) {
            AWOW_LOG_ERROR("DATA", "SpellDataTable: Failed to read header from: " + filepath);
            return false;
        }

        // Read all records, but only store relevant ones
        size_t loaded = 0;
        for (uint32_t i = 0; i < count; ++i) {
            SpellDataRecord record;
            file.read(reinterpret_cast<char*>(&record), sizeof(SpellDataRecord));

            if (!file) {
                AWOW_LOG_ERROR("DATA", "SpellDataTable: Failed to read record " + std::to_string(i) + " from: " + filepath);
                break;
            }

            // Only load if this spell is in the requested set
            if (spell_ids.count(record.spell_id) > 0) {
                SpellInfo info;
                info.spell_id = record.spell_id;
                info.gcd_ms = record.gcd_ms;
                info.cooldown_ms = record.cooldown_ms;
                info.cast_time_ms = record.cast_time_ms;
                info.gcd_haste_affected = (record.gcd_haste_affected != 0);
                info.cd_haste_affected = (record.cd_haste_affected != 0);
                info.base_charges = record.base_charges;

                spells_[info.spell_id] = info;
                loaded++;
            }
        }

        return true;
    }

    const SpellInfo* SpellDataTable::getSpellInfo(uint32_t spell_id) const {
        auto it = spells_.find(spell_id);
        if (it != spells_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    int32_t SpellDataTable::getBaseCooldown(uint32_t spell_id) const {
        const auto* info = getSpellInfo(spell_id);
        return info ? info->cooldown_ms : 0;
    }

    int32_t SpellDataTable::getBaseGCD(uint32_t spell_id) const {
        const auto* info = getSpellInfo(spell_id);
        return info ? info->gcd_ms : 0;
    }

    bool SpellDataTable::isCooldownHasteAffected(uint32_t spell_id) const {
        const auto* info = getSpellInfo(spell_id);
        return info ? info->cd_haste_affected : false;
    }

    uint8_t SpellDataTable::getBaseCharges(uint32_t spell_id) const {
        const auto* info = getSpellInfo(spell_id);
        return info ? info->base_charges : 0;
    }

} // namespace awow
