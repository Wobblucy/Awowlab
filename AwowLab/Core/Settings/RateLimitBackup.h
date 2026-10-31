#pragma once

#include <cstdint>

/**
 * Multi-location backup for rate limit data.
 *
 * Stores rate limit in multiple locations to prevent easy bypass:
 * 1. Windows Registry (HKEY_CURRENT_USER\Software\AwowLab)
 * 2. CSV hidden row (map_world_bounds.csv with fake mapId 99999)
 * 3. Gzip trailing bytes (spell_names.csv.gz after EOF)
 *
 * All locations are checked on load; the most restrictive value wins.
 *
 * Only the main app wires this into the settings cache (see
 * registerRateLimitBackups below); overlay-only builds never touch it.
 */
class RateLimitBackup {
public:
    struct RateLimitData {
        int queriesUsed = 0;
        int64_t windowStartTime = 0;
    };

    // === Registry backup (Windows only) ===
    static bool saveToRegistry(const RateLimitData& data);
    static bool loadFromRegistry(RateLimitData& data);
    static void clearRegistry();

    // === CSV hidden row backup (map_world_bounds.csv) ===
    static bool saveToCsvHiddenRow(const RateLimitData& data);
    static bool loadFromCsvHiddenRow(RateLimitData& data);

    // === Gzip trailing bytes backup (spell_names.csv.gz) ===
    static bool saveToGzipTrailing(const RateLimitData& data);
    static bool loadFromGzipTrailing(RateLimitData& data);

    // === Convenience methods ===
    /** Save to all backup locations (best-effort, ignores failures) */
    static void saveToAllBackups(const RateLimitData& data);

    /** Load from all backups and return most restrictive valid value */
    static RateLimitData loadMostRestrictive();

private:
    // XOR key for simple obfuscation (not cryptographic security)
    static constexpr uint8_t XOR_KEY[] = {0x4A, 0x7B, 0x2C, 0x9D, 0x5E, 0x1F, 0x8A, 0x3B, 0x6C, 0xAD, 0x0E, 0xDF};
    static constexpr uint32_t MAGIC = 0x4C525741; // "AWRL" in little-endian
    static constexpr int HIDDEN_MAP_ID = 99999;

#ifdef _WIN32
    static constexpr const wchar_t* REG_KEY_PATH = L"Software\\AwowLab";
    static constexpr const wchar_t* REG_VALUE_NAME = L"RateLimitData";
#endif
};

namespace awow {

/**
 * Wire the multi-location rate limit backup into the settings cache.
 * Call before SettingsCache::initialize() so the restore-on-load step
 * can consult the backups.
 */
void registerRateLimitBackups();

} // namespace awow
