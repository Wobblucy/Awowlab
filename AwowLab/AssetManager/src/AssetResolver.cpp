#include "AssetResolver.h"
#include <algorithm>

namespace awow {

std::string AssetResolver::resolve(const std::string& basePath, const std::string& filename) {
    std::string userPath = getUserPath(basePath, filename);
    if (std::filesystem::exists(userPath)) {
        return userPath;
    }

    std::string defaultPath = getDefaultsPath(basePath, filename);
    if (std::filesystem::exists(defaultPath)) {
        return defaultPath;
    }

    return userPath;
}

std::string AssetResolver::getUserPath(const std::string& basePath, const std::string& filename) {
    std::filesystem::path base(basePath);
    std::filesystem::path file(filename);
    return (base / file).string();
}

std::string AssetResolver::getDefaultsPath(const std::string& basePath, const std::string& filename) {
    std::filesystem::path base(basePath);
    std::filesystem::path file(filename);
    return (base / "defaults" / file).string();
}

bool AssetResolver::hasUserOverride(const std::string& basePath, const std::string& filename) {
    return std::filesystem::exists(getUserPath(basePath, filename));
}

bool AssetResolver::hasDefault(const std::string& basePath, const std::string& filename) {
    return std::filesystem::exists(getDefaultsPath(basePath, filename));
}

std::string AssetResolver::sanitizeFilename(const std::string& name) {
    std::string sanitized = name;
    const std::string illegalChars = "/\\:*?\"<>|";

    for (char& c : sanitized) {
        if (illegalChars.find(c) != std::string::npos) {
            c = '_';
        }
    }

    return sanitized;
}

bool AssetResolver::ensureDirectoryExists(const std::string& filePath) {
    std::filesystem::path dir = std::filesystem::path(filePath).parent_path();
    if (dir.empty()) {
        return true;
    }
    if (std::filesystem::exists(dir)) {
        return true;
    }
    std::error_code ec;
    return std::filesystem::create_directories(dir, ec);
}

} // namespace awow
