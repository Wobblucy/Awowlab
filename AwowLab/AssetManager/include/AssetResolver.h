#pragma once

#include <string>
#include <filesystem>

namespace awow {

// User files override the shipped defaults:
//   user:    {basePath}/{filename}
//   default: {basePath}/defaults/{filename}
// e.g. resolve("triggers", "Sikran.json") gives triggers/Sikran.json when it
// exists, otherwise triggers/defaults/Sikran.json.
class AssetResolver {
public:
    // filename may include subdirectories. When neither file exists the user
    // path is returned, so errors name the place a user file would go.
    static std::string resolve(const std::string& basePath, const std::string& filename);

    // Where saves go
    static std::string getUserPath(const std::string& basePath, const std::string& filename);

    static std::string getDefaultsPath(const std::string& basePath, const std::string& filename);

    static bool hasUserOverride(const std::string& basePath, const std::string& filename);

    static bool hasDefault(const std::string& basePath, const std::string& filename);

    // Replaces / \ : * ? " < > | with '_'
    static std::string sanitizeFilename(const std::string& name);

    // Creates the parent directory of filePath if needed
    static bool ensureDirectoryExists(const std::string& filePath);
};

} // namespace awow
