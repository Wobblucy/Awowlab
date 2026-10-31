#include "UI/SpellGCDDatabase.h"
#include <fstream>
#include <sstream>
#include <charconv>

// Binary format matches SpellDataTable.cpp
#pragma pack(push, 1)
struct SpellDataRecord {
    uint32_t spell_id;
    int32_t  gcd_ms;
    int32_t  cooldown_ms;
    int32_t  cast_time_ms;
    uint8_t  gcd_haste_affected;
    uint8_t  cd_haste_affected;
    uint8_t  padding[2];
};
#pragma pack(pop)

bool SpellGCDDatabase::loadFromBinary(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    spellTimings_.clear();

    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    if (!file) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        SpellDataRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(SpellDataRecord));
        if (!file) {
            break;
        }

        spellTimings_[record.spell_id] = SpellTimingData{
            static_cast<uint32_t>(record.gcd_ms),
            static_cast<uint32_t>(record.cooldown_ms),
            static_cast<uint32_t>(record.cast_time_ms)
        };
    }

    return !spellTimings_.empty();
}

bool SpellGCDDatabase::loadFromCSV(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    spellTimings_.clear();

    std::string line;
    bool isHeader = true;

    while (std::getline(file, line)) {
        // Skip empty lines
        if (line.empty()) continue;

        // Skip header row
        if (isHeader) {
            isHeader = false;
            continue;
        }

        // Parse CSV: spell_id,name,gcd_ms,cooldown_ms,cast_time_ms[,class]
        std::istringstream iss(line);
        std::string token;

        // Field 0: spell_id
        if (!std::getline(iss, token, ',')) continue;
        uint32_t spellId = 0;
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), spellId);
        if (ec != std::errc()) continue;

        // Field 1: name (skip)
        if (!std::getline(iss, token, ',')) continue;

        // Field 2: gcd_ms
        if (!std::getline(iss, token, ',')) continue;
        uint32_t gcdMs = 0;
        if (!token.empty()) {
            std::from_chars(token.data(), token.data() + token.size(), gcdMs);
        }

        // Field 3: cooldown_ms
        if (!std::getline(iss, token, ',')) continue;
        uint32_t cooldownMs = 0;
        if (!token.empty()) {
            std::from_chars(token.data(), token.data() + token.size(), cooldownMs);
        }

        // Field 4: cast_time_ms (optional)
        uint32_t castTimeMs = 0;
        if (std::getline(iss, token, ',') && !token.empty()) {
            std::from_chars(token.data(), token.data() + token.size(), castTimeMs);
        }

        spellTimings_[spellId] = SpellTimingData{gcdMs, cooldownMs, castTimeMs};
    }

    return !spellTimings_.empty();
}

SpellTimingData SpellGCDDatabase::getSpellTiming(uint32_t spellId) const {
    auto it = spellTimings_.find(spellId);
    if (it != spellTimings_.end()) {
        return it->second;
    }
    // Return default GCD for unknown spells (no cooldown, instant cast)
    return SpellTimingData{DEFAULT_GCD_MS, 0, 0};
}

uint32_t SpellGCDDatabase::getGCD(uint32_t spellId) const {
    return getSpellTiming(spellId).gcd_ms;
}

uint32_t SpellGCDDatabase::getCooldown(uint32_t spellId) const {
    return getSpellTiming(spellId).cooldown_ms;
}

bool SpellGCDDatabase::isOffGCD(uint32_t spellId) const {
    return getGCD(spellId) == 0;
}

bool SpellGCDDatabase::hasSpellData(uint32_t spellId) const {
    return spellTimings_.find(spellId) != spellTimings_.end();
}
