#pragma once

#include "IExportableSettings.h"
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <rapidjson/document.h>

// Manages export/import of settings to/from plaintext JSON files
// Settings classes register with this manager to appear in the Export/Import UI
class SettingsExportManager {
public:
    static SettingsExportManager& instance();

    // Register an exportable settings provider
    // The manager does not take ownership - provider must outlive the manager
    void registerProvider(IExportableSettings* provider);

    // Unregister a provider (optional, for cleanup)
    void unregisterProvider(const std::string& key);

    // Get list of available categories for UI (key, displayName pairs)
    std::vector<std::pair<std::string, std::string>> getAvailableCategories() const;

    // Export selected categories to file
    // Returns true on success
    bool exportToFile(const std::string& filepath, const std::set<std::string>& selectedKeys) const;

    // Probe a file to see which categories it contains
    // Returns set of keys found in the file
    std::set<std::string> probeFile(const std::string& filepath) const;

    // Import selected categories from file
    // Only imports categories that are both in selectedKeys and present in the file
    // Returns number of categories successfully imported
    size_t importFromFile(const std::string& filepath, const std::set<std::string>& selectedKeys);

    // Get the default export directory
    static std::string getExportDirectory();

    // Generate a default filename with timestamp
    static std::string generateDefaultFilename();

private:
    SettingsExportManager() = default;
    ~SettingsExportManager() = default;
    SettingsExportManager(const SettingsExportManager&) = delete;
    SettingsExportManager& operator=(const SettingsExportManager&) = delete;

    std::unordered_map<std::string, IExportableSettings*> providers_;

    // File format version
    static constexpr int FILE_VERSION = 1;
};
