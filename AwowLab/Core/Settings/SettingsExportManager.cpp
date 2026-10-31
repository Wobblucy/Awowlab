#include "SettingsExportManager.h"
#include "Core/ErrorLogger.h"
#include "Core/Utils/FileNameUtils.h"
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>

SettingsExportManager& SettingsExportManager::instance() {
    static SettingsExportManager instance;
    return instance;
}

void SettingsExportManager::registerProvider(IExportableSettings* provider) {
    if (provider) {
        providers_[provider->getExportKey()] = provider;
    }
}

void SettingsExportManager::unregisterProvider(const std::string& key) {
    providers_.erase(key);
}

std::vector<std::pair<std::string, std::string>> SettingsExportManager::getAvailableCategories() const {
    std::vector<std::pair<std::string, std::string>> categories;
    categories.reserve(providers_.size());

    for (const auto& [key, provider] : providers_) {
        categories.emplace_back(key, provider->getDisplayName());
    }

    // Sort by display name for consistent UI ordering
    std::sort(categories.begin(), categories.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    return categories;
}

bool SettingsExportManager::exportToFile(const std::string& filepath, const std::set<std::string>& selectedKeys) const {
    if (selectedKeys.empty()) {
        AWOW_LOG_WARNING("SETTINGS", "No categories selected for export");
        return false;
    }

    // Ensure directory exists. The error_code overloads keep a bad path a
    // failed export instead of an uncaught exception.
    const std::filesystem::path path = awow::files::pathFromUtf8(filepath);
    std::filesystem::path dir = path.parent_path();
    std::error_code ec;
    if (!dir.empty() && !std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            AWOW_LOG_ERROR("SETTINGS", "Failed to create settings export folder: " + ec.message());
            return false;
        }
    }

    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", FILE_VERSION, alloc);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    rapidjson::Value timestamp;
    timestamp.SetString(oss.str().c_str(), static_cast<rapidjson::SizeType>(oss.str().length()), alloc);
    doc.AddMember("exportedAt", timestamp, alloc);

    rapidjson::Value categories(rapidjson::kObjectType);

    size_t exportedCount = 0;
    for (const auto& key : selectedKeys) {
        auto it = providers_.find(key);
        if (it == providers_.end()) continue;

        rapidjson::Value categoryData = it->second->exportToJson(alloc);
        rapidjson::Value keyValue;
        keyValue.SetString(key.c_str(), static_cast<rapidjson::SizeType>(key.length()), alloc);
        categories.AddMember(keyValue, categoryData, alloc);
        ++exportedCount;
    }

    doc.AddMember("categories", categories, alloc);

    std::ofstream file(path);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("SETTINGS", "Failed to open file for settings export: " + filepath);
        return false;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    file << buffer.GetString();
    file.close();
    if (file.fail()) {
        AWOW_LOG_ERROR("SETTINGS", "Failed to write settings export: " + filepath);
        return false;
    }

    AWOW_LOG_INFO("SETTINGS", "Exported " + std::to_string(exportedCount) + " categories to " + filepath);
    return true;
}

std::set<std::string> SettingsExportManager::probeFile(const std::string& filepath) const {
    std::set<std::string> foundKeys;

    const std::filesystem::path path = awow::files::pathFromUtf8(filepath);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return foundKeys;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return foundKeys;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        return foundKeys;
    }

    if (!doc.HasMember("version") || !doc["version"].IsInt()) {
        return foundKeys;
    }
    int version = doc["version"].GetInt();
    if (version > FILE_VERSION) {
        AWOW_LOG_WARNING("SETTINGS", "Settings file version " + std::to_string(version) + " is newer than supported version " + std::to_string(FILE_VERSION));
        return foundKeys;
    }

    if (!doc.HasMember("categories") || !doc["categories"].IsObject()) {
        return foundKeys;
    }

    const auto& categories = doc["categories"];
    for (auto it = categories.MemberBegin(); it != categories.MemberEnd(); ++it) {
        // Only offer entries that are actually importable - anything else
        // would trip the object accessors in the category importers
        if (it->value.IsObject()) {
            foundKeys.insert(it->name.GetString());
        }
    }

    return foundKeys;
}

size_t SettingsExportManager::importFromFile(const std::string& filepath, const std::set<std::string>& selectedKeys) {
    if (selectedKeys.empty()) {
        return 0;
    }

    const std::filesystem::path path = awow::files::pathFromUtf8(filepath);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        AWOW_LOG_ERROR("SETTINGS", "Settings import file not found: " + filepath);
        return 0;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("SETTINGS", "Failed to open settings import file: " + filepath);
        return 0;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        AWOW_LOG_ERROR("SETTINGS", "Failed to parse settings import JSON: " + filepath);
        return 0;
    }

    if (!doc.HasMember("version") || !doc["version"].IsInt()) {
        AWOW_LOG_ERROR("SETTINGS", "Missing or invalid version in settings import file: " + filepath);
        return 0;
    }
    if (doc["version"].GetInt() > FILE_VERSION) {
        AWOW_LOG_ERROR("SETTINGS", "Settings import file is from a newer app version: " + filepath);
        return 0;
    }

    if (!doc.HasMember("categories") || !doc["categories"].IsObject()) {
        AWOW_LOG_ERROR("SETTINGS", "Missing categories object in settings import file: " + filepath);
        return 0;
    }

    const auto& categories = doc["categories"];
    size_t importedCount = 0;

    for (const auto& key : selectedKeys) {
        if (!categories.HasMember(key.c_str())) {
            continue;
        }

        auto it = providers_.find(key);
        if (it == providers_.end()) {
            continue;
        }

        // Import the category. Skip non-object entries - the importers
        // index into the value and would assert on anything else.
        const auto& categoryData = categories[key.c_str()];
        if (!categoryData.IsObject()) {
            AWOW_LOG_ERROR("SETTINGS", "Settings category is not an object, skipping: " + key);
            continue;
        }
        if (it->second->importFromJson(categoryData)) {
            ++importedCount;
        } else {
            AWOW_LOG_ERROR("SETTINGS", "Failed to import settings category: " + key);
        }
    }

    AWOW_LOG_INFO("SETTINGS", "Imported " + std::to_string(importedCount) + " categories from " + filepath);
    return importedCount;
}

std::string SettingsExportManager::getExportDirectory() {
    return "exported_settings";
}

std::string SettingsExportManager::generateDefaultFilename() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << "settings_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S") << ".json";
    return oss.str();
}
