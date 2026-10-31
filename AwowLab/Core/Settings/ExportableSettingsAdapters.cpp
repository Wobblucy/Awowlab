#include "ExportableSettingsAdapters.h"
#include "UI/ActorAppearanceSettings.h"
#include "UI/SpellAssignmentSettings.h"
#include "UI/SpellGroupSettings.h"
#include "UI/NameplateSettings.h"
#include "UI/FacingIndicatorSettings.h"
#include "UI/BossTimerSettings.h"
#include "UI/Panels/Frames/UIRaidFramePanel.h"
#include "UI/UISettings.h"
#include "Core/UnifiedSettings.h"

// ActorAppearanceExporter
rapidjson::Value ActorAppearanceExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->serializeToString();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool ActorAppearanceExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    bool result = settings_->deserializeFromString(json["data"].GetString());
    if (result) settings_->saveToFile();
    return result;
}

// SpellAssignmentExporter (singleton)
rapidjson::Value SpellAssignmentExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = spell_assignment::SpellAssignmentSettings::instance().serializeToJson();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool SpellAssignmentExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    bool result = spell_assignment::SpellAssignmentSettings::instance().deserializeFromJson(json["data"].GetString());
    if (result) spell_assignment::SpellAssignmentSettings::instance().saveToSettings();
    return result;
}

// SpellGroupExporter
rapidjson::Value SpellGroupExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->serializeToJson();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool SpellGroupExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    bool result = settings_->deserializeFromJson(json["data"].GetString());
    if (result) settings_->saveToSettings();
    return result;
}

// NameplateSettingsExporter
rapidjson::Value NameplateSettingsExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->toJson();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool NameplateSettingsExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    settings_->fromJson(json["data"].GetString());
    auto& cache = SettingsCache::instance();
    cache.get().nameplateSettingsJson = settings_->toJson();
    cache.markDirty();
    return true;
}

// FacingIndicatorExporter
rapidjson::Value FacingIndicatorExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->toJson();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool FacingIndicatorExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    settings_->fromJson(json["data"].GetString());
    auto& cache = SettingsCache::instance();
    cache.get().facingIndicatorSettingsJson = settings_->toJson();
    cache.markDirty();
    return true;
}

// BossTimerExporter
rapidjson::Value BossTimerExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->serializeToJson();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool BossTimerExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    *settings_ = BossTimerSettings::deserializeFromJson(json["data"].GetString());
    auto& cache = SettingsCache::instance();
    cache.get().bossTimerSettingsJson = settings_->serializeToJson();
    cache.markDirty();
    return true;
}

// RaidFrameConfigExporter
rapidjson::Value RaidFrameConfigExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->toJson();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool RaidFrameConfigExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    settings_->fromJson(json["data"].GetString());
    auto& cache = SettingsCache::instance();
    cache.get().raidFrameConfigJson = settings_->toJson();
    cache.markDirty();
    return true;
}

// UILayoutExporter
rapidjson::Value UILayoutExporter::exportToJson(rapidjson::Document::AllocatorType& alloc) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    std::string data = settings_->serializeToString();
    rapidjson::Value dataVal;
    dataVal.SetString(data.c_str(), static_cast<rapidjson::SizeType>(data.length()), alloc);
    obj.AddMember("data", dataVal, alloc);
    return obj;
}

bool UILayoutExporter::importFromJson(const rapidjson::Value& json) {
    if (!json.HasMember("data") || !json["data"].IsString()) return false;
    bool result = settings_->deserializeFromString(json["data"].GetString());
    if (result) settings_->saveToFile();
    return result;
}
