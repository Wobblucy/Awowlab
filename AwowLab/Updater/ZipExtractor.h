#pragma once

#include <string>
#include <functional>

namespace awow::updater {

// fileIndex is 0-based.
using ZipProgressCallback = std::function<void(const std::string& currentFile, int fileIndex, int totalFiles)>;

// Extracts ZIP archives with miniz.
class ZipExtractor {
public:
    static bool extract(const std::string& zipPath,
                        const std::string& destDir,
                        ZipProgressCallback progress = nullptr);

    static const std::string& getLastError();

private:
    static thread_local std::string lastError_;
};

} // namespace awow::updater
