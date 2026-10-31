#include "RateLimitBackup.h"
#include "WclSettings.h"
#include "../UnifiedSettings.h"
#include "../Utils/ExecutablePath.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <ctime>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

// ============================================================================
// XOR Encryption/Decryption Helper
// ============================================================================
static void xorEncrypt(uint8_t* data, size_t len, const uint8_t* key, size_t keyLen) {
    for (size_t i = 0; i < len; ++i) {
        data[i] ^= key[i % keyLen];
    }
}

// ============================================================================
// Windows Registry Implementation
// ============================================================================
#ifdef _WIN32

bool RateLimitBackup::saveToRegistry(const RateLimitData& data) {
    HKEY hKey;
    LONG result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        REG_KEY_PATH,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &hKey,
        nullptr
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    // Pack data into binary blob
    struct PackedData {
        int32_t queriesUsed;
        int64_t windowStartTime;
    };

    PackedData packed;
    packed.queriesUsed = data.queriesUsed;
    packed.windowStartTime = data.windowStartTime;

    result = RegSetValueExW(
        hKey,
        REG_VALUE_NAME,
        0,
        REG_BINARY,
        reinterpret_cast<const BYTE*>(&packed),
        sizeof(packed)
    );

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool RateLimitBackup::loadFromRegistry(RateLimitData& data) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        REG_KEY_PATH,
        0,
        KEY_READ,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    struct PackedData {
        int32_t queriesUsed;
        int64_t windowStartTime;
    };

    PackedData packed;
    DWORD dataSize = sizeof(packed);
    DWORD dataType;

    result = RegQueryValueExW(
        hKey,
        REG_VALUE_NAME,
        nullptr,
        &dataType,
        reinterpret_cast<BYTE*>(&packed),
        &dataSize
    );

    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || dataType != REG_BINARY || dataSize != sizeof(packed)) {
        return false;
    }

    data.queriesUsed = packed.queriesUsed;
    data.windowStartTime = packed.windowStartTime;
    return true;
}

void RateLimitBackup::clearRegistry() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        REG_KEY_PATH,
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, REG_VALUE_NAME);
        RegCloseKey(hKey);
    }
}

#else

// Non-Windows stubs for registry
bool RateLimitBackup::saveToRegistry(const RateLimitData&) {
    return true;  // No-op on non-Windows
}

bool RateLimitBackup::loadFromRegistry(RateLimitData&) {
    return false;  // No backup available on non-Windows
}

void RateLimitBackup::clearRegistry() {
    // No-op on non-Windows
}

#endif

// ============================================================================
// CSV Hidden Row Implementation (map_world_bounds.csv)
// ============================================================================

bool RateLimitBackup::saveToCsvHiddenRow(const RateLimitData& data) {
    std::string csvPath = awow::getExecutableDirectory() + "PNG\\Worldmaps\\map_world_bounds.csv";

    // Read existing file
    std::ifstream inFile(csvPath);
    if (!inFile.is_open()) {
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    bool foundHiddenRow = false;

    while (std::getline(inFile, line)) {
        // Check if this is our hidden row (starts with HIDDEN_MAP_ID)
        if (line.find(std::to_string(HIDDEN_MAP_ID) + ",") == 0) {
            foundHiddenRow = true;
            // Replace with updated data
            // Format: mapId,minX,minY,maxX,maxY,width,height
            // We encode: mapId=99999, minX=queriesUsed, minY=timestampHigh, maxX=timestampLow, maxY=checksum
            int32_t timestampHigh = static_cast<int32_t>(data.windowStartTime >> 32);
            int32_t timestampLow = static_cast<int32_t>(data.windowStartTime & 0xFFFFFFFF);
            int32_t checksum = data.queriesUsed ^ timestampHigh ^ timestampLow ^ 0x5A5A5A5A;

            std::ostringstream oss;
            oss << HIDDEN_MAP_ID << ","
                << data.queriesUsed << ".0,"
                << timestampHigh << ".0,"
                << timestampLow << ".0,"
                << checksum << ".0,"
                << "1024,768";
            lines.push_back(oss.str());
        } else {
            lines.push_back(line);
        }
    }
    inFile.close();

    // If hidden row wasn't found, append it
    if (!foundHiddenRow) {
        int32_t timestampHigh = static_cast<int32_t>(data.windowStartTime >> 32);
        int32_t timestampLow = static_cast<int32_t>(data.windowStartTime & 0xFFFFFFFF);
        int32_t checksum = data.queriesUsed ^ timestampHigh ^ timestampLow ^ 0x5A5A5A5A;

        std::ostringstream oss;
        oss << HIDDEN_MAP_ID << ","
            << data.queriesUsed << ".0,"
            << timestampHigh << ".0,"
            << timestampLow << ".0,"
            << checksum << ".0,"
            << "1024,768";
        lines.push_back(oss.str());
    }

    // Write back
    std::ofstream outFile(csvPath);
    if (!outFile.is_open()) {
        return false;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        outFile << lines[i];
        if (i < lines.size() - 1) {
            outFile << "\n";
        }
    }
    outFile.close();

    return true;
}

bool RateLimitBackup::loadFromCsvHiddenRow(RateLimitData& data) {
    std::string csvPath = awow::getExecutableDirectory() + "PNG\\Worldmaps\\map_world_bounds.csv";

    std::ifstream file(csvPath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Look for our hidden row
        if (line.find(std::to_string(HIDDEN_MAP_ID) + ",") == 0) {
            // Parse: mapId,queriesUsed.0,timestampHigh.0,timestampLow.0,checksum.0,width,height
            std::istringstream iss(line);
            std::string token;
            std::vector<std::string> tokens;

            while (std::getline(iss, token, ',')) {
                tokens.push_back(token);
            }

            if (tokens.size() >= 5) {
                // Parse values (they have .0 suffix to look like floats).
                // The file is user-editable; a value that doesn't parse
                // means this backup is unusable, not that the app should crash.
                int queriesUsed = 0;
                int32_t timestampHigh = 0;
                int32_t timestampLow = 0;
                int32_t storedChecksum = 0;
                try {
                    queriesUsed = static_cast<int>(std::stof(tokens[1]));
                    timestampHigh = static_cast<int32_t>(std::stof(tokens[2]));
                    timestampLow = static_cast<int32_t>(std::stof(tokens[3]));
                    storedChecksum = static_cast<int32_t>(std::stof(tokens[4]));
                } catch (const std::exception&) {
                    return false;
                }

                // Verify checksum
                int32_t expectedChecksum = queriesUsed ^ timestampHigh ^ timestampLow ^ 0x5A5A5A5A;
                if (storedChecksum == expectedChecksum) {
                    data.queriesUsed = queriesUsed;
                    data.windowStartTime = (static_cast<int64_t>(timestampHigh) << 32) |
                                           (static_cast<uint32_t>(timestampLow));
                    return true;
                }
            }
        }
    }

    return false;
}

// ============================================================================
// Gzip Trailing Bytes Implementation (spell_names.csv.gz)
// ============================================================================

bool RateLimitBackup::saveToGzipTrailing(const RateLimitData& data) {
    std::string gzPath = awow::getExecutableDirectory() + "data\\spellnames\\spell_names.csv.gz";

    // Read existing file
    std::ifstream inFile(gzPath, std::ios::binary | std::ios::ate);
    if (!inFile.is_open()) {
        return false;
    }

    std::streamsize fileSize = inFile.tellg();
    inFile.seekg(0, std::ios::beg);

    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (!inFile.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        return false;
    }
    inFile.close();

    // Check if we already have trailing data (look for MAGIC at expected position)
    // Trailing format: [MAGIC:4][version:1][encrypted_payload:12][checksum:4] = 21 bytes
    const size_t TRAILER_SIZE = 21;
    size_t originalSize = fileData.size();

    // Remove existing trailer if present
    if (originalSize >= TRAILER_SIZE) {
        uint32_t possibleMagic;
        std::memcpy(&possibleMagic, &fileData[originalSize - TRAILER_SIZE], sizeof(possibleMagic));
        if (possibleMagic == MAGIC) {
            originalSize -= TRAILER_SIZE;
            fileData.resize(originalSize);
        }
    }

    // Create new trailer
    // Pack data manually to ensure 12 bytes regardless of compiler padding
    uint8_t payloadBytes[12];
    int32_t queriesUsedPacked = data.queriesUsed;
    int64_t windowStartTimePacked = data.windowStartTime;
    std::memcpy(payloadBytes, &queriesUsedPacked, 4);
    std::memcpy(payloadBytes + 4, &windowStartTimePacked, 8);

    // XOR encrypt payload
    uint8_t encryptedPayload[12];
    std::memcpy(encryptedPayload, payloadBytes, 12);
    xorEncrypt(encryptedPayload, 12, XOR_KEY, sizeof(XOR_KEY));

    // Calculate checksum of original (unencrypted) payload
    uint32_t crc = 0;
    for (size_t i = 0; i < 12; ++i) {
        crc ^= payloadBytes[i];
        crc = (crc << 1) | (crc >> 31);  // Simple rotate-XOR checksum
    }

    // Append trailer
    uint8_t version = 0x01;
    fileData.resize(originalSize + TRAILER_SIZE);
    size_t pos = originalSize;
    std::memcpy(&fileData[pos], &MAGIC, 4); pos += 4;
    fileData[pos++] = version;
    std::memcpy(&fileData[pos], encryptedPayload, 12); pos += 12;
    std::memcpy(&fileData[pos], &crc, 4);

    // Write back
    std::ofstream outFile(gzPath, std::ios::binary);
    if (!outFile.is_open()) {
        return false;
    }
    outFile.write(reinterpret_cast<char*>(fileData.data()), fileData.size());
    outFile.close();

    return true;
}

bool RateLimitBackup::loadFromGzipTrailing(RateLimitData& data) {
    std::string gzPath = awow::getExecutableDirectory() + "data\\spellnames\\spell_names.csv.gz";

    std::ifstream file(gzPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    const size_t TRAILER_SIZE = 21;

    if (fileSize < static_cast<std::streamsize>(TRAILER_SIZE)) {
        return false;
    }

    // Read trailer
    file.seekg(-static_cast<std::streamoff>(TRAILER_SIZE), std::ios::end);
    uint8_t trailer[TRAILER_SIZE];
    if (!file.read(reinterpret_cast<char*>(trailer), TRAILER_SIZE)) {
        return false;
    }
    file.close();

    // Check magic
    uint32_t magic;
    std::memcpy(&magic, trailer, 4);
    if (magic != MAGIC) {
        return false;
    }

    // Check version
    uint8_t version = trailer[4];
    if (version != 0x01) {
        return false;
    }

    // Decrypt payload
    uint8_t encryptedPayload[12];
    std::memcpy(encryptedPayload, &trailer[5], 12);
    xorEncrypt(encryptedPayload, 12, XOR_KEY, sizeof(XOR_KEY));

    // Verify CRC
    uint32_t storedCrc;
    std::memcpy(&storedCrc, &trailer[17], 4);

    uint32_t calculatedCrc = 0;
    for (size_t i = 0; i < 12; ++i) {
        calculatedCrc ^= encryptedPayload[i];
        calculatedCrc = (calculatedCrc << 1) | (calculatedCrc >> 31);
    }

    if (storedCrc != calculatedCrc) {
        return false;
    }

    // Extract data (decrypted payload is now in encryptedPayload)
    int32_t queriesUsedPacked;
    int64_t windowStartTimePacked;
    std::memcpy(&queriesUsedPacked, encryptedPayload, 4);
    std::memcpy(&windowStartTimePacked, encryptedPayload + 4, 8);

    data.queriesUsed = queriesUsedPacked;
    data.windowStartTime = windowStartTimePacked;
    return true;
}

// ============================================================================
// Convenience Methods
// ============================================================================

void RateLimitBackup::saveToAllBackups(const RateLimitData& data) {
    // Save to all locations, ignore failures (best-effort)
    saveToRegistry(data);
    saveToCsvHiddenRow(data);
    saveToGzipTrailing(data);
}

RateLimitBackup::RateLimitData RateLimitBackup::loadMostRestrictive() {
    RateLimitData result;
    result.queriesUsed = 0;
    result.windowStartTime = 0;

    auto now = static_cast<int64_t>(std::time(nullptr));
    constexpr int64_t WINDOW_SECONDS = 3600;  // 1 hour

    auto isValid = [now](const RateLimitData& d) {
        if (d.windowStartTime == 0) return false;
        return (now - d.windowStartTime) < WINDOW_SECONDS;
    };

    // Check all sources
    RateLimitData regData, csvData, gzData;

    if (loadFromRegistry(regData) && isValid(regData)) {
        if (regData.queriesUsed > result.queriesUsed) {
            result = regData;
        }
    }

    if (loadFromCsvHiddenRow(csvData) && isValid(csvData)) {
        if (csvData.queriesUsed > result.queriesUsed) {
            result = csvData;
        }
    }

    if (loadFromGzipTrailing(gzData) && isValid(gzData)) {
        if (gzData.queriesUsed > result.queriesUsed) {
            result = gzData;
        }
    }

    return result;
}

// ============================================================================
// Settings cache wiring
// ============================================================================

void awow::registerRateLimitBackups() {
    SettingsPersistenceHooks hooks;

    // Restore-on-load: check every backup location and keep the most
    // restrictive rate limit. This prevents bypassing the limit by
    // deleting individual backup files.
    hooks.afterLoad = [](UnifiedSettings::AllSettings& settings) {
        auto backupData = RateLimitBackup::loadMostRestrictive();
        if (backupData.windowStartTime == 0) {
            return;  // no valid backup data - keep the file values
        }

        auto wcl = WclSettings::readFrom(settings);

        auto now = std::chrono::system_clock::now();
        auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        bool fileInCurrentWindow = (nowSeconds - wcl.windowStartTime) < 3600;

        if (fileInCurrentWindow) {
            // Both valid - use higher queriesUsed
            if (backupData.queriesUsed > wcl.queriesUsed) {
#ifndef NDEBUG
                std::cout << "Using backup rate limit (higher): "
                          << backupData.queriesUsed << " queries.\n";
#endif
                wcl.queriesUsed = backupData.queriesUsed;
                wcl.windowStartTime = backupData.windowStartTime;
                wcl.writeTo(settings);
            }
        } else {
            // Only backup is current - use it
#ifndef NDEBUG
            std::cout << "Using backup rate limit (file expired).\n";
#endif
            wcl.queriesUsed = backupData.queriesUsed;
            wcl.windowStartTime = backupData.windowStartTime;
            wcl.writeTo(settings);
        }
        // If the file is current but backups are expired, or both are
        // expired, keep the file values (maybeResetWindow handles expiry)
    };

    // Save-on-save: mirror the current rate limit to every backup location
    hooks.afterSave = [](const UnifiedSettings::AllSettings& settings) {
        auto wcl = WclSettings::readFrom(settings);
        RateLimitBackup::RateLimitData data;
        data.queriesUsed = wcl.queriesUsed;
        data.windowStartTime = wcl.windowStartTime;
        RateLimitBackup::saveToAllBackups(data);
    };

    SettingsCache::instance().setPersistenceHooks(std::move(hooks));
}

