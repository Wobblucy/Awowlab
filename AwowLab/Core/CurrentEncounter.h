#pragma once
#include <string>
#include <cstdint>
#include <algorithm>
#include <cctype>

// Simple singleton to store current encounter info for global access
// Avoids passing encounter data through multiple layers
class CurrentEncounter {
public:
    static CurrentEncounter& instance() {
        static CurrentEncounter instance;
        return instance;
    }

    void set(const std::string& name, uint32_t encounterId = 0,
             uint16_t difficulty = 0, uint16_t mapId = 0,
             int32_t durationMs = 0) {
        encounterName_ = name;
        encounterId_ = encounterId;
        difficulty_ = difficulty;
        mapId_ = mapId;
        durationMs_ = durationMs;
    }

    // Whether the pull killed the boss. Local logs learn this from
    // ENCOUNTER_END, WCL reports from the fight metadata; either way it cannot
    // be derived from the events, so whoever loaded the fight has to say.
    void setKill(bool killed) {
        kill_ = killed;
        killKnown_ = true;
    }

    // Set M+ specific info
    void setMythicPlusInfo(uint32_t keystoneLevel) {
        keystoneLevel_ = keystoneLevel;
        isMythicPlus_ = true;
    }

    // Update map ID (called when map is loaded/determined by UI)
    void setMapId(uint16_t mapId) {
        mapId_ = mapId;
    }

    void clear() {
        encounterName_.clear();
        encounterId_ = 0;
        difficulty_ = 0;
        mapId_ = 0;
        durationMs_ = 0;
        keystoneLevel_ = 0;
        isMythicPlus_ = false;
        kill_ = false;
        killKnown_ = false;
    }

    const std::string& getName() const { return encounterName_; }
    uint32_t getId() const { return encounterId_; }
    uint16_t getDifficulty() const { return difficulty_; }
    uint16_t getMapId() const { return mapId_; }
    int32_t getDurationMs() const { return durationMs_; }
    uint32_t getKeystoneLevel() const { return keystoneLevel_; }
    bool isMythicPlus() const { return isMythicPlus_; }
    bool wasKill() const { return kill_; }
    // False for a trash pull or an unloaded fight, where "not a kill" and
    // "nobody said" are different things
    bool isKillKnown() const { return killKnown_; }
    bool hasEncounter() const { return !encounterName_.empty(); }

    // Generate a default filename for snapshot export
    // Format: "Name_startMs_durationMs.awsnap"
    // For M+: "DungeonName +Level_startMs_durationMs.awsnap"
    std::string generateSnapshotFilename(int32_t windowStartMs, int32_t windowEndMs) const {
        std::string name = encounterName_;
        if (name.empty()) {
            name = "Snapshot";
        }

        // Add keystone level for M+
        if (isMythicPlus_ && keystoneLevel_ > 0) {
            name += " +" + std::to_string(keystoneLevel_);
        }

        // Sanitize filename (remove/replace invalid characters)
        std::string sanitized;
        for (char c : name) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                sanitized += '_';
            } else {
                sanitized += c;
            }
        }

        int32_t durationMs = windowEndMs - windowStartMs;
        return sanitized + "_" + std::to_string(windowStartMs) + "_" +
               std::to_string(durationMs) + ".awsnap";
    }

private:
    CurrentEncounter() = default;
    std::string encounterName_;
    uint32_t encounterId_ = 0;
    uint16_t difficulty_ = 0;
    uint16_t mapId_ = 0;
    int32_t durationMs_ = 0;
    uint32_t keystoneLevel_ = 0;
    bool isMythicPlus_ = false;
    bool kill_ = false;
    bool killKnown_ = false;
};
