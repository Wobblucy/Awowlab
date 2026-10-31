#include "UI/BossTimerSettings.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include "Core/ErrorLogger.h"

std::string BossTimerSettings::serializeToJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", 2, alloc);
    doc.AddMember("enabled", enabled, alloc);
    doc.AddMember("showCasts", showCasts, alloc);
    doc.AddMember("showDamage", showDamage, alloc);
    doc.AddMember("showDebuffs", showDebuffs, alloc);
    doc.AddMember("iconSize", iconSize, alloc);
    doc.AddMember("groupingWindowMs", groupingWindowMs, alloc);

    // Upcoming casts panel settings
    doc.AddMember("upcomingCastsPanelEnabled", upcomingCastsPanelEnabled, alloc);
    doc.AddMember("upcomingWindowSeconds", upcomingWindowSeconds, alloc);
    doc.AddMember("upcomingIconSize", upcomingIconSize, alloc);
    doc.AddMember("upcomingPanelWidth", upcomingPanelWidth, alloc);

    // Serialize enabled spell IDs as array (whitelist)
    rapidjson::Value enabledArray(rapidjson::kArrayType);
    for (uint32_t id : enabledSpellIds) {
        enabledArray.PushBack(id, alloc);
    }
    doc.AddMember("enabledSpellIds", enabledArray, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

BossTimerSettings BossTimerSettings::deserializeFromJson(const std::string& json) {
    BossTimerSettings settings;

    if (json.empty()) {
        return settings;
    }

    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        AWOW_LOG_INFO("SETTINGS", std::string("Boss timer settings JSON parse error: ") +
                                      rapidjson::GetParseError_En(doc.GetParseError()));
        return settings;
    }

    if (doc.HasMember("enabled") && doc["enabled"].IsBool()) {
        settings.enabled = doc["enabled"].GetBool();
    }
    if (doc.HasMember("showCasts") && doc["showCasts"].IsBool()) {
        settings.showCasts = doc["showCasts"].GetBool();
    }
    if (doc.HasMember("showDamage") && doc["showDamage"].IsBool()) {
        settings.showDamage = doc["showDamage"].GetBool();
    }
    if (doc.HasMember("showDebuffs") && doc["showDebuffs"].IsBool()) {
        settings.showDebuffs = doc["showDebuffs"].GetBool();
    }
    if (doc.HasMember("iconSize") && doc["iconSize"].IsNumber()) {
        settings.iconSize = doc["iconSize"].GetFloat();
    }
    if (doc.HasMember("groupingWindowMs") && doc["groupingWindowMs"].IsUint()) {
        settings.groupingWindowMs = doc["groupingWindowMs"].GetUint();
    }

    // Upcoming casts panel settings
    if (doc.HasMember("upcomingCastsPanelEnabled") && doc["upcomingCastsPanelEnabled"].IsBool()) {
        settings.upcomingCastsPanelEnabled = doc["upcomingCastsPanelEnabled"].GetBool();
    }
    if (doc.HasMember("upcomingWindowSeconds") && doc["upcomingWindowSeconds"].IsNumber()) {
        settings.upcomingWindowSeconds = doc["upcomingWindowSeconds"].GetFloat();
    }
    if (doc.HasMember("upcomingIconSize") && doc["upcomingIconSize"].IsNumber()) {
        settings.upcomingIconSize = doc["upcomingIconSize"].GetFloat();
    }
    if (doc.HasMember("upcomingPanelWidth") && doc["upcomingPanelWidth"].IsNumber()) {
        settings.upcomingPanelWidth = doc["upcomingPanelWidth"].GetFloat();
    }

    if (doc.HasMember("enabledSpellIds") && doc["enabledSpellIds"].IsArray()) {
        const auto& arr = doc["enabledSpellIds"];
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
            if (arr[i].IsUint()) {
                settings.enabledSpellIds.insert(arr[i].GetUint());
            }
        }
    }

    return settings;
}
