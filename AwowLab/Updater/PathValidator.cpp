#include "PathValidator.h"

#include <filesystem>
#include <algorithm>
#include <cctype>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace awow::updater {

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Resolved, with any trailing separator removed, so "C:\Users\me\Downloads\"
// and "c:/users/me/downloads" compare equal.
std::string normalisedFolder(const std::string& folder) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::path(folder), ec);
    if (ec) {
        resolved = fs::path(folder);
    }
    std::string s = resolved.string();
    std::replace(s.begin(), s.end(), '/', '\\');
    while (s.size() > 1 && s.back() == '\\') {
        s.pop_back();
    }
    return toLower(s);
}

bool isSameFolder(const std::string& a, const std::string& b) {
    std::error_code ec;
    if (fs::equivalent(fs::path(a), fs::path(b), ec) && !ec) {
        return true;
    }
    return normalisedFolder(a) == normalisedFolder(b);
}

bool isDriveRoot(const std::string& folder) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::path(folder), ec);
    if (ec) {
        resolved = fs::path(folder);
    }

    int parts = 0;
    for (const auto& part : resolved.relative_path()) {
        if (!part.empty() && part != ".") {
            ++parts;
        }
    }

    // For \\server\share the share name is the first relative part.
    std::string rootName = resolved.root_name().string();
    bool isShare = rootName.size() >= 2 &&
                   (rootName[0] == '\\' || rootName[0] == '/') &&
                   (rootName[1] == '\\' || rootName[1] == '/');

    return isShare ? parts <= 1 : parts == 0;
}

std::string knownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR widePath = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &widePath))) {
        try {
            result = fs::path(widePath).string();
        } catch (const std::exception&) {
            // Not representable in the ANSI code page the rest of the updater uses.
            result.clear();
        }
    }
    CoTaskMemFree(widePath);
    return result;
}

} // namespace

bool PathValidator::containsTraversal(const std::string& path) {
    // Only a whole ".." component counts as traversal. Icon names such as
    // "spell_shadow_devouringplague..png" contain two dots legitimately.

    size_t len = path.length();
    size_t i = 0;

    while (i < len) {
        while (i < len && (path[i] == '/' || path[i] == '\\')) {
            ++i;
        }

        size_t start = i;
        while (i < len && path[i] != '/' && path[i] != '\\') {
            ++i;
        }

        if (i - start == 2 && path[start] == '.' && path[start + 1] == '.') {
            return true;
        }
    }

    // URL-encoded dots (%2e).
    const std::string lower = toLower(path);
    if (lower.find("%2e%2e") != std::string::npos ||
        lower.find("%2e.") != std::string::npos ||
        lower.find(".%2e") != std::string::npos) {
        return true;
    }

    return false;
}

bool PathValidator::isAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (path[0] == '/') {
        return true;
    }

    if (path[0] == '\\') {
        return true;
    }

    if (path.length() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return true;
    }

    return false;
}

bool PathValidator::isPathSafe(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // A null byte would truncate the path.
    if (path.find('\0') != std::string::npos) {
        return false;
    }

    // Covers rooted paths such as "\Windows" as well as drive letters.
    if (isAbsolutePath(path)) {
        return false;
    }

    if (containsTraversal(path)) {
        return false;
    }

    return true;
}

std::string PathValidator::validateAndCanonicalize(
    const std::string& baseDir,
    const std::string& relativePath) {

    if (!isPathSafe(relativePath)) {
        return "";
    }

    if (baseDir.empty()) {
        return "";
    }

    try {
        fs::path basePath = fs::weakly_canonical(fs::path(baseDir));

        fs::path fullPath = basePath / relativePath;

        // weakly_canonical, because the target usually doesn't exist yet.
        fs::path canonicalPath = fs::weakly_canonical(fullPath);

        std::string baseStr = basePath.string();
        std::string fullStr = canonicalPath.string();

        std::replace(baseStr.begin(), baseStr.end(), '/', '\\');
        std::replace(fullStr.begin(), fullStr.end(), '/', '\\');

        // The resolved path must still sit under the base directory.
        if (fullStr.length() < baseStr.length()) {
            return "";
        }

        // Windows paths compare case-insensitively.
        if (!toLower(fullStr).starts_with(toLower(baseStr))) {
            return "";
        }

        // The match has to end at a separator, or "C:\App" would accept
        // "C:\Application".
        if (fullStr.length() > baseStr.length()) {
            char nextChar = fullStr[baseStr.length()];
            if (nextChar != '\\' && nextChar != '/') {
                return "";
            }
        }

        return canonicalPath.string();

    } catch (const std::exception&) {
        // Any filesystem error rejects the path.
        return "";
    }
}

InstallFolder::Status InstallFolder::check(const std::string& folder,
                                           const std::string& downloadsFolder) {
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(fs::path(folder), ec)) {
        return Status::Missing;
    }

    if (isDriveRoot(folder)) {
        return Status::DriveRoot;
    }

    if (!downloadsFolder.empty() && isSameFolder(folder, downloadsFolder)) {
        return Status::DownloadsFolder;
    }

    if (!isWritable(folder)) {
        return Status::NotWritable;
    }

    if (isExistingInstall(folder)) {
        return Status::ExistingInstall;
    }

    return Status::Usable;
}

bool InstallFolder::isExistingInstall(const std::string& folder) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(folder) / "AwowLab.exe", ec);
}

bool InstallFolder::isWritable(const std::string& folder) {
    fs::path probe = fs::path(folder) /
        (L".awowlab-write-test-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));

    HANDLE handle = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    CloseHandle(handle);
    DeleteFileW(probe.c_str());
    return true;
}

std::string InstallFolder::knownDownloadsFolder() {
    return knownFolderPath(FOLDERID_Downloads);
}

std::string InstallFolder::defaultInstallFolder() {
    std::string localAppData = knownFolderPath(FOLDERID_LocalAppData);
    if (localAppData.empty()) {
        return "";
    }
    return (fs::path(localAppData) / "AwowLab").string();
}

} // namespace awow::updater
