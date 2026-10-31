#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

/**
 * All application settings in one AES-256-CBC encrypted file. If the file is
 * missing, corrupt or from another format version, every setting starts from
 * its default.
 */
class UnifiedSettings {
public:
    struct AllSettings {
        // Window settings
        int windowWidth = 1600;
        int windowHeight = 900;
        bool windowMaximized = false;

        // UI scaling
        float fontScale = 1.0f;  // Range: 0.5 to 2.0

        // Locale/language setting (e.g., "en_US", "ko_KR")
        std::string locale = "en_US";

        // The JSON-string fields below are owned and parsed by their panels
        std::string uiLayoutJson;

        // Actor appearance
        std::string actorAppearanceJson;

        // Spell grouping, per spec
        std::string spellGroupsJson;

        // Spell assignment: per-class display locations
        std::string spellAssignmentJson;

        // Which spell data files to load from data/, by game version. Names
        // spell_data_{version}.bin, cooldown_modifiers_{version}.json and
        // haste_buffs_{version}.json, so a version with no matching files loads
        // no cooldowns, no GCDs and no haste buffs.
        std::string spellDataVersion = "12.1";

        // Nameplates
        std::string nameplateSettingsJson;

        // Raid frames
        std::string raidFrameConfigJson;

        // Boss timers
        std::string bossTimerSettingsJson;

        // Facing indicator
        std::string facingIndicatorSettingsJson;

        // Mob damage weighting
        std::string mobWeightSettingsJson;

        // Boss phase rules
        std::string phaseSettingsJson;

        // Meter bars use WoW class colors for players whose spec is
        // known (from COMBATANT_INFO); off falls back to the generated
        // per-actor colors
        bool meterClassColors = true;

        // Custom model rotations, as key=value lines
        std::string customModelRotationsJson;

        // Overlay logs folder, so the user picks it once
        std::string overlayLogsFolder;

        // Spell ids the user wants tracked as defensives. The death recap
        // shows, for each of these, whether the dying player pressed it in
        // the last 10s or had it active at death. Empty by default (the
        // feature shows nothing until the user adds ids in Settings), and
        // parsing only stores aura applies/removes for ids in this list, so
        // an empty list costs nothing.
        std::vector<uint32_t> trackedDefensiveSpellIds;

        // Top-level file entries not handled by the fields above, kept as
        // raw JSON (entry name -> JSON value text). They are read on load
        // and written back on save, so optional features can keep their
        // own values here and builds with different feature sets can share
        // one settings file without losing each other's data.
        std::map<std::string, std::string> extraValues;
    };

    // False if the file is missing or unreadable; settings is left at defaults
    static bool load(AllSettings& settings);

    static bool save(const AllSettings& settings);

    static bool exists();

    // For tests and reset
    static void clear();

    static std::filesystem::path getFilePath();

    // SHA-256 of the material. Public so tests can pin the derivation.
    static std::vector<uint8_t> deriveKey(std::string_view material);

private:
    static constexpr const char* FILENAME = "User Settings/settings.dat";
    static constexpr uint32_t MAGIC = 0x574F5741;  // "AWOW" in little-endian
    static constexpr uint32_t VERSION = 1;
    static constexpr size_t IV_SIZE = 16;
    static constexpr size_t KEY_SIZE = 32;  // AES-256

    // The key comes from settings_key in the builder's config, compiled into
    // the binary, so this keeps the file unreadable to casual inspection
    // rather than secret.
    static std::vector<uint8_t> deriveKey();

    // Fills iv with a fresh random IV; empty result on failure
    static std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& iv);

    // Empty result on failure
    static std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv);

    static std::string serializeToJson(const AllSettings& settings);

    static bool deserializeFromJson(const std::string& json, AllSettings& settings);
};

/**
 * Optional callbacks the host application can register around settings
 * persistence. The main app uses these to reconcile and mirror some of
 * its values outside the settings file; the standalone overlay registers
 * nothing and works with the file alone.
 */
struct SettingsPersistenceHooks {
    /** Runs at the end of initialize(), after the file has been read.
     *  May adjust the freshly loaded settings in place. */
    std::function<void(UnifiedSettings::AllSettings&)> afterLoad;

    /** Runs after every successful save to disk. */
    std::function<void(const UnifiedSettings::AllSettings&)> afterSave;
};

/**
 * In-memory copy of the settings that every component reads and writes,
 * instead of calling UnifiedSettings::load/save. After changing a value,
 * call markDirty(); tick(), called each frame, writes the file once
 * AUTO_SAVE_INTERVAL_SECONDS have passed since the last save.
 */
class SettingsCache {
public:
    static constexpr double AUTO_SAVE_INTERVAL_SECONDS = 10.0;

    static SettingsCache& instance();

    SettingsCache(const SettingsCache&) = delete;
    SettingsCache& operator=(const SettingsCache&) = delete;
    SettingsCache(SettingsCache&&) = delete;
    SettingsCache& operator=(SettingsCache&&) = delete;

    /**
     * Register persistence callbacks (see SettingsPersistenceHooks).
     * Call before initialize() so the load step can run them.
     * Without a registration, loads and saves touch only the settings file.
     */
    void setPersistenceHooks(SettingsPersistenceHooks hooks);

    // Call once at startup
    void initialize();

    // Call markDirty() after modifying
    UnifiedSettings::AllSettings& get() { return settings_; }

    const UnifiedSettings::AllSettings& get() const { return settings_; }

    void markDirty();

    bool isDirty() const { return dirty_; }

    // Call each frame; saves when dirty and the interval has passed
    void tick();

    // Saves now if dirty. Call on exit.
    void flush();

    // Saves now even if nothing is dirty
    void forceSave();

private:
    SettingsCache() = default;
    ~SettingsCache();

    UnifiedSettings::AllSettings settings_;
    SettingsPersistenceHooks hooks_;
    std::atomic<bool> dirty_{false};
    std::chrono::steady_clock::time_point lastSaveTime_;
    bool initialized_{false};
    mutable std::mutex mutex_;
};
