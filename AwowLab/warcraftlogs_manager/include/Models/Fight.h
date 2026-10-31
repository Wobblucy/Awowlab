#pragma once

#include <cstdint>
#include <string>

namespace warcraftlogs {

// Warcraft Logs numbers difficulties its own way, and the numbers overlap with
// WoW's: WCL's 5 is Mythic raid, WoW's 5 is 10-player Heroic from Cataclysm. A
// WCL value passed to code expecting WoW's would get a wrong name rather than
// none, so the value is translated here and the app only sees WoW's numbering.
inline uint32_t wclDifficultyToWow(int32_t wclDifficulty) {
    switch (wclDifficulty) {
        case 1:  return 17;   // LFR
        case 2:  return 14;   // Normal (older reports)
        case 3:  return 14;   // Normal
        case 4:  return 15;   // Heroic
        case 5:  return 16;   // Mythic
        case 10: return 8;    // Mythic Keystone
        default: return 0;    // unknown
    }
}

struct Fight {
    int32_t id = 0;              // Fight ID (unique within report)
    std::string name;            // Boss/encounter name
    int64_t startTime = 0;       // Start time in milliseconds (relative to log start)
    int64_t endTime = 0;         // End time in milliseconds
    int32_t encounterID = 0;     // WoW encounter ID (0 for trash/wipes)
    int32_t difficulty = 0;      // WCL numbering: 5 Mythic, 4 Heroic, 3 Normal, 1 LFR, 10 M+
    int32_t keystoneLevel = 0;   // M+ keystone level (0 if not M+)

    // Whether the pull actually killed the boss. Local logs carry this on
    // ENCOUNTER_END, but a WCL report only states it here - the event stream has
    // an encounterstart and no matching end, so there is nothing to infer it
    // from once the metadata is gone.
    bool kill = false;

    [[nodiscard]] int64_t duration() const noexcept {
        return endTime - startTime;
    }

    [[nodiscard]] double durationSeconds() const noexcept {
        return static_cast<double>(duration()) / 1000.0;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return duration() > 0;
    }

    [[nodiscard]] bool isBossEncounter() const noexcept {
        return encounterID > 0;
    }

    [[nodiscard]] bool isMythicPlus() const noexcept {
        return keystoneLevel > 0 || difficulty == 10;
    }

    // Takes WarcraftLogs difficulty values, which differ from WoW's ids.
    [[nodiscard]] std::string difficultyName() const {
        switch (difficulty) {
            case 1:  return "LFR";
            case 3:  return "Normal";
            case 4:  return "Heroic";
            case 5:  return "Mythic";
            case 10: return "M+";
            // Older and WoW-numbered values
            case 2:  return "Heroic";
            case 6:  return "25 Heroic";
            case 7:  return "LFR";
            case 8:  return "M+ Keystone";
            case 9:  return "40 Player";
            case 14: return "Normal";
            case 15: return "Heroic";
            case 16: return "Mythic";
            case 17: return "LFR";
            case 23: return "Mythic";
            case 24: return "Timewalking";
            default: return "";
        }
    }
};

} // namespace warcraftlogs
