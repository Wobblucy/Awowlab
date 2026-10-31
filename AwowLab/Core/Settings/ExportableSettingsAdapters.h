#pragma once

#include "IExportableSettings.h"
#include <rapidjson/document.h>
#include <string>

// Forward declarations - actual types defined in UI headers
class ActorAppearanceSettings;
struct UISettings;
struct NameplateSettings;
struct FacingIndicatorSettings;
struct BossTimerSettings;
struct RaidFrameConfig;

namespace spell_assignment {
    class SpellAssignmentSettings;
}

namespace spell_grouping {
    class SpellGroupSettings;
}

// Adapter that wraps ActorAppearanceSettings to implement IExportableSettings
class ActorAppearanceExporter : public IExportableSettings {
public:
    explicit ActorAppearanceExporter(ActorAppearanceSettings* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "actor_appearance"; }
    std::string getDisplayName() const override { return "Actor Appearance"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    ActorAppearanceSettings* settings_;
};

// Adapter for SpellAssignmentSettings (singleton)
class SpellAssignmentExporter : public IExportableSettings {
public:
    std::string getExportKey() const override { return "spell_assignments"; }
    std::string getDisplayName() const override { return "Spell Assignments"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;
};

// Adapter for SpellGroupSettings
class SpellGroupExporter : public IExportableSettings {
public:
    explicit SpellGroupExporter(spell_grouping::SpellGroupSettings* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "spell_groups"; }
    std::string getDisplayName() const override { return "Spell Groups"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    spell_grouping::SpellGroupSettings* settings_;
};

// Adapter for NameplateSettings
class NameplateSettingsExporter : public IExportableSettings {
public:
    explicit NameplateSettingsExporter(NameplateSettings* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "nameplate_settings"; }
    std::string getDisplayName() const override { return "Nameplate Settings"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    NameplateSettings* settings_;
};

// Adapter for FacingIndicatorSettings
class FacingIndicatorExporter : public IExportableSettings {
public:
    explicit FacingIndicatorExporter(FacingIndicatorSettings* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "facing_indicator"; }
    std::string getDisplayName() const override { return "Facing Indicator"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    FacingIndicatorSettings* settings_;
};

// Adapter for BossTimerSettings
class BossTimerExporter : public IExportableSettings {
public:
    explicit BossTimerExporter(BossTimerSettings* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "boss_timers"; }
    std::string getDisplayName() const override { return "Boss Timers"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    BossTimerSettings* settings_;
};

// Adapter for RaidFrameConfig
class RaidFrameConfigExporter : public IExportableSettings {
public:
    explicit RaidFrameConfigExporter(RaidFrameConfig* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "raid_frame_config"; }
    std::string getDisplayName() const override { return "Raid Frame Config"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    RaidFrameConfig* settings_;
};

// Adapter for UISettings (window layout)
class UILayoutExporter : public IExportableSettings {
public:
    explicit UILayoutExporter(UISettings* settings) : settings_(settings) {}

    std::string getExportKey() const override { return "ui_layout"; }
    std::string getDisplayName() const override { return "UI Layout"; }

    rapidjson::Value exportToJson(rapidjson::Document::AllocatorType& alloc) const override;
    bool importFromJson(const rapidjson::Value& json) override;

private:
    UISettings* settings_;
};
