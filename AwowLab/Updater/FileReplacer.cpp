#include "FileReplacer.h"
#include "HashVerifier.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace awow::updater {

thread_local std::string FileReplacer::lastError_;

bool FileReplacer::ensureDirectoryExists(const std::string& filePath) {
    std::filesystem::path path(filePath);
    std::filesystem::path parentDir = path.parent_path();

    std::error_code ec;
    if (parentDir.empty() || std::filesystem::exists(parentDir, ec)) {
        return true;
    }

    if (!std::filesystem::create_directories(parentDir, ec)) {
        lastError_ = "Failed to create directory: " + parentDir.string() + " - " + ec.message();
        return false;
    }

    return true;
}

bool FileReplacer::atomicReplace(const std::string& targetPath,
                                  const std::string& tempPath,
                                  const std::string& expectedHash) {
    // Every filesystem call here takes an error_code: a file another process
    // holds open must come back as a failed update, not an exception.
    std::error_code ec;
    if (!std::filesystem::exists(tempPath, ec)) {
        lastError_ = "Temp file does not exist: " + tempPath;
        return false;
    }

    if (!HashVerifier::verifyFile(tempPath, expectedHash)) {
        lastError_ = "Hash verification failed: " + HashVerifier::getLastError();
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::string backupPath = getBackupPath(targetPath);

    if (!ensureDirectoryExists(targetPath)) {
        return false;
    }

    bool backedUp = false;
    if (std::filesystem::exists(targetPath, ec)) {
        if (std::filesystem::exists(backupPath, ec)) {
            std::filesystem::remove(backupPath, ec);
        }

        std::filesystem::rename(targetPath, backupPath, ec);
        if (ec) {
            lastError_ = "Failed to create backup: " + ec.message();
            return false;
        }
        backedUp = true;
    }

    std::filesystem::rename(tempPath, targetPath, ec);
    if (ec) {
        lastError_ = "Failed to replace file: " + ec.message();
        if (backedUp) {
            std::error_code restoreEc;
            std::filesystem::rename(backupPath, targetPath, restoreEc);
            if (restoreEc) {
                lastError_ += "; the original is left at " + backupPath +
                              " (restore failed: " + restoreEc.message() + ")";
            }
        }
        return false;
    }

    // AwowLab.exe.bak is kept so a broken app update can be undone by hand.
    std::filesystem::path targetFilename = std::filesystem::path(targetPath).filename();
    if (targetFilename != "AwowLab.exe") {
        std::filesystem::remove(backupPath, ec);
    }

    return true;
}

bool FileReplacer::rollback(const std::string& targetPath) {
    std::string backupPath = getBackupPath(targetPath);

    std::error_code ec;
    if (!std::filesystem::exists(backupPath, ec)) {
        lastError_ = "No backup file found: " + backupPath;
        return false;
    }

    if (std::filesystem::exists(targetPath, ec)) {
        std::filesystem::remove(targetPath, ec);
        if (ec) {
            lastError_ = "Failed to remove target for rollback: " + ec.message();
            return false;
        }
    }

    std::filesystem::rename(backupPath, targetPath, ec);
    if (ec) {
        lastError_ = "Failed to restore from backup: " + ec.message();
        return false;
    }

    return true;
}

void FileReplacer::cleanupBackups(const std::string& directory) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            if (ext == BAK_SUFFIX) {
                if (entry.path().stem() != "AwowLab.exe") {
                    std::filesystem::remove(entry.path(), ec);
                }
            }
        }
    }
}

void FileReplacer::cleanupTempFiles(const std::string& directory) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            if (ext == TMP_SUFFIX) {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
}

const std::string& FileReplacer::getLastError() {
    return lastError_;
}

} // namespace awow::updater
