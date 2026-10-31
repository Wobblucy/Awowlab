#include "UI/NameplateSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>

namespace {
    // Helper to serialize NameplateCategorySettings to a JSON object
    rapidjson::Value serializeCategorySettings(const NameplateCategorySettings& settings,
                                                rapidjson::Document::AllocatorType& alloc) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("fontScale", settings.fontScale, alloc);
        obj.AddMember("showBackground", settings.showBackground, alloc);
        obj.AddMember("useActorColor", settings.useActorColor, alloc);
        obj.AddMember("backgroundOpacity", settings.backgroundOpacity, alloc);
        obj.AddMember("maxNameLength", settings.maxNameLength, alloc);

        rapidjson::Value colorArray(rapidjson::kArrayType);
        colorArray.PushBack(settings.textColor[0], alloc);
        colorArray.PushBack(settings.textColor[1], alloc);
        colorArray.PushBack(settings.textColor[2], alloc);
        colorArray.PushBack(settings.textColor[3], alloc);
        obj.AddMember("textColor", colorArray, alloc);

        return obj;
    }

    // Helper to deserialize NameplateCategorySettings from a JSON object
    void deserializeCategorySettings(NameplateCategorySettings& settings,
                                      const rapidjson::Value& obj) {
        if (!obj.IsObject()) return;

        if (obj.HasMember("fontScale") && obj["fontScale"].IsNumber()) {
            settings.fontScale = obj["fontScale"].GetFloat();
            settings.fontScale = std::clamp(settings.fontScale, 0.5f, 2.0f);
        }
        if (obj.HasMember("showBackground") && obj["showBackground"].IsBool()) {
            settings.showBackground = obj["showBackground"].GetBool();
        }
        if (obj.HasMember("useActorColor") && obj["useActorColor"].IsBool()) {
            settings.useActorColor = obj["useActorColor"].GetBool();
        }
        if (obj.HasMember("backgroundOpacity") && obj["backgroundOpacity"].IsNumber()) {
            settings.backgroundOpacity = obj["backgroundOpacity"].GetFloat();
            settings.backgroundOpacity = std::clamp(settings.backgroundOpacity, 0.0f, 1.0f);
        }
        if (obj.HasMember("textColor") && obj["textColor"].IsArray()) {
            const auto& arr = obj["textColor"];
            if (arr.Size() >= 4) {
                settings.textColor[0] = arr[0].GetFloat();
                settings.textColor[1] = arr[1].GetFloat();
                settings.textColor[2] = arr[2].GetFloat();
                settings.textColor[3] = arr[3].GetFloat();
            }
        }
        if (obj.HasMember("maxNameLength") && obj["maxNameLength"].IsInt()) {
            settings.maxNameLength = obj["maxNameLength"].GetInt();
            settings.maxNameLength = std::clamp(settings.maxNameLength, 0, 20);
        }
    }
}

std::string NameplateSettings::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    // Global toggles
    doc.AddMember("showNameplates", showNameplates, alloc);
    doc.AddMember("showHealthBars", showHealthBars, alloc);
    doc.AddMember("showAuras", showAuras, alloc);
    doc.AddMember("auraIconSize", auraIconSize, alloc);
    doc.AddMember("showLastCast", showLastCast, alloc);
    doc.AddMember("lastCastIconSize", lastCastIconSize, alloc);

    // Per-category settings
    doc.AddMember("playerSettings", serializeCategorySettings(playerSettings, alloc), alloc);
    doc.AddMember("otherSettings", serializeCategorySettings(otherSettings, alloc), alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

void NameplateSettings::fromJson(const std::string& json) {
    if (json.empty()) {
        return;
    }

    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        return;
    }

    // Global toggles
    if (doc.HasMember("showNameplates") && doc["showNameplates"].IsBool()) {
        showNameplates = doc["showNameplates"].GetBool();
    }
    if (doc.HasMember("showHealthBars") && doc["showHealthBars"].IsBool()) {
        showHealthBars = doc["showHealthBars"].GetBool();
    }
    if (doc.HasMember("showAuras") && doc["showAuras"].IsBool()) {
        showAuras = doc["showAuras"].GetBool();
    }
    if (doc.HasMember("auraIconSize") && doc["auraIconSize"].IsNumber()) {
        auraIconSize = doc["auraIconSize"].GetFloat();
        auraIconSize = std::clamp(auraIconSize, 16.0f, 48.0f);
    }
    if (doc.HasMember("showLastCast") && doc["showLastCast"].IsBool()) {
        showLastCast = doc["showLastCast"].GetBool();
    }
    if (doc.HasMember("lastCastIconSize") && doc["lastCastIconSize"].IsNumber()) {
        lastCastIconSize = doc["lastCastIconSize"].GetFloat();
        lastCastIconSize = std::clamp(lastCastIconSize, 16.0f, 32.0f);
    }

    // Per-category settings
    if (doc.HasMember("playerSettings")) {
        deserializeCategorySettings(playerSettings, doc["playerSettings"]);
    }
    if (doc.HasMember("otherSettings")) {
        deserializeCategorySettings(otherSettings, doc["otherSettings"]);
    }
}
