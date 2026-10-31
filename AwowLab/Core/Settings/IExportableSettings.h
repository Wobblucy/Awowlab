#pragma once

#include <string>
#include <rapidjson/document.h>

// Interface for settings classes that can be exported/imported
// Implement this interface to make a settings class available in the Export/Import UI
class IExportableSettings {
public:
    virtual ~IExportableSettings() = default;

    // Unique key for this settings type (e.g., "actor_appearance", "spell_assignments")
    // Used as the JSON key in export files
    virtual std::string getExportKey() const = 0;

    // Human-readable name for UI display (e.g., "Actor Appearance", "Spell Assignments")
    virtual std::string getDisplayName() const = 0;

    // Serialize settings to JSON value (for export)
    virtual rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const = 0;

    // Deserialize settings from JSON value (for import). Returns false on failure.
    virtual bool importFromJson(const rapidjson::Value& json) = 0;
};
