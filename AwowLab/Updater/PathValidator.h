#pragma once

#include <string>

namespace awow::updater {

// Checks paths that come from downloaded data (manifest file paths, archive
// destinations, ZIP entry names) so none of them can write outside the
// install folder, e.g. "../../Windows/malware.dll".
class PathValidator {
public:
    // False for paths with a ".." component, an absolute or rooted path,
    // or an embedded null byte.
    static bool isPathSafe(const std::string& path);

    // Joins relativePath onto baseDir, resolves it, and returns the result
    // only if it is still inside baseDir. Empty string otherwise.
    static std::string validateAndCanonicalize(
        const std::string& baseDir,
        const std::string& relativePath);

    // A ".." path component, or a URL-encoded one (%2e%2e).
    static bool containsTraversal(const std::string& path);

    // C:\..., \\server\share, \... or /...
    static bool isAbsolutePath(const std::string& path);
};

// Checks the folder picked for a fresh install. The standalone updater is
// usually run from Downloads, and installing beside it would leave the app
// living there, next to whatever else the browser has saved.
class InstallFolder {
public:
    enum class Status {
        Usable,
        ExistingInstall,   // already holds AwowLab.exe; update it in place
        Missing,           // not an existing folder
        DriveRoot,         // C:\ or \\server\share
        DownloadsFolder,
        NotWritable,       // e.g. Program Files; the updater never runs elevated
    };

    // downloadsFolder is passed in so tests don't depend on the real one;
    // the updater passes knownDownloadsFolder().
    static Status check(const std::string& folder, const std::string& downloadsFolder);

    static bool isExistingInstall(const std::string& folder);

    // Creates and deletes a small temp file in the folder.
    static bool isWritable(const std::string& folder);

    // The user's Downloads folder, or empty if Windows can't say.
    static std::string knownDownloadsFolder();

    // %LOCALAPPDATA%\AwowLab, or empty if Windows can't say.
    static std::string defaultInstallFolder();
};

} // namespace awow::updater
