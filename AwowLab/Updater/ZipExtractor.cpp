#include "ZipExtractor.h"
#include "PathValidator.h"
#include "miniz.h"

#include <algorithm>
#include <filesystem>

namespace awow::updater {
namespace fs = std::filesystem;

thread_local std::string ZipExtractor::lastError_;

bool ZipExtractor::extract(const std::string& zipPath,
                           const std::string& destDir,
                           ZipProgressCallback progress) {
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        lastError_ = "Failed to create destination directory: " + ec.message();
        return false;
    }

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) {
        lastError_ = "Failed to open ZIP file: " + zipPath;
        return false;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);

    if (progress) {
        progress("Starting extraction...", 0, static_cast<int>(numFiles));
    }

    bool success = true;

    for (mz_uint i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            lastError_ = "Failed to get file info for index " + std::to_string(i);
            success = false;
            break;
        }

        std::string fileName = fileStat.m_filename;

        // Entry names come from the archive, so they get the same traversal
        // check as manifest paths.
        std::string destPath = PathValidator::validateAndCanonicalize(destDir, fileName);
        if (destPath.empty()) {
            lastError_ = "Security: Path traversal detected in ZIP entry: " + fileName;
            success = false;
            break;
        }

        std::replace(destPath.begin(), destPath.end(), '/', '\\');

        if (progress) {
            progress(fileName, static_cast<int>(i), static_cast<int>(numFiles));
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            fs::create_directories(destPath, ec);
            continue;
        }

        fs::path parentPath = fs::path(destPath).parent_path();
        if (!parentPath.empty()) {
            fs::create_directories(parentPath, ec);
        }

        if (!mz_zip_reader_extract_to_file(&zip, i, destPath.c_str(), 0)) {
            lastError_ = "Failed to extract: " + fileName;
            success = false;
            break;
        }
    }

    mz_zip_reader_end(&zip);

    if (success && progress) {
        progress("Extraction complete", static_cast<int>(numFiles), static_cast<int>(numFiles));
    }

    return success;
}

const std::string& ZipExtractor::getLastError() {
    return lastError_;
}

} // namespace awow::updater
