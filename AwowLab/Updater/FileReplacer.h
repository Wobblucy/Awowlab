#pragma once

#include <string>
#include <filesystem>

namespace awow::updater {

// Replaces a file with a rollback path: the download lands in file.tmp and is
// hash-checked, the old file is renamed to file.bak, the .tmp is renamed over
// the target, then the .bak is removed.
//
// An interrupted update leaves a recognisable state. A .tmp without a .bak is
// an incomplete download and can be retried; a .bak without the target can be
// restored with rollback().
class FileReplacer {
public:
    static constexpr const char* TMP_SUFFIX = ".tmp";
    static constexpr const char* BAK_SUFFIX = ".bak";

    // Deletes tempPath and fails if its SHA256 doesn't match expectedHash.
    static bool atomicReplace(const std::string& targetPath,
                              const std::string& tempPath,
                              const std::string& expectedHash);

    // Creates the parent directories of filePath.
    static bool ensureDirectoryExists(const std::string& filePath);

    static bool rollback(const std::string& targetPath);

    static void cleanupBackups(const std::string& directory);
    static void cleanupTempFiles(const std::string& directory);

    static std::string getTempPath(const std::string& targetPath) {
        return targetPath + TMP_SUFFIX;
    }

    static std::string getBackupPath(const std::string& targetPath) {
        return targetPath + BAK_SUFFIX;
    }

    static const std::string& getLastError();

private:
    static thread_local std::string lastError_;
};

} // namespace awow::updater
