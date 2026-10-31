#include "UI/FacingIndicatorSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>

std::string FacingIndicatorSettings::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("enabled", enabled, alloc);
    doc.AddMember("opacity", opacity, alloc);
    doc.AddMember("size", size, alloc);

    rapidjson::Value colorArray(rapidjson::kArrayType);
    colorArray.PushBack(color[0], alloc);
    colorArray.PushBack(color[1], alloc);
    colorArray.PushBack(color[2], alloc);
    doc.AddMember("color", colorArray, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

void FacingIndicatorSettings::fromJson(const std::string& json) {
    if (json.empty()) {
        return;
    }

    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        return;
    }

    if (doc.HasMember("enabled") && doc["enabled"].IsBool()) {
        enabled = doc["enabled"].GetBool();
    }
    if (doc.HasMember("opacity") && doc["opacity"].IsNumber()) {
        opacity = doc["opacity"].GetFloat();
        opacity = std::clamp(opacity, 0.0f, 1.0f);
    }
    if (doc.HasMember("size") && doc["size"].IsNumber()) {
        size = doc["size"].GetFloat();
        size = std::clamp(size, 0.5f, 5.0f);
    }
    if (doc.HasMember("color") && doc["color"].IsArray()) {
        const auto& arr = doc["color"];
        if (arr.Size() >= 3) {
            color[0] = std::clamp(arr[0].GetFloat(), 0.0f, 1.0f);
            color[1] = std::clamp(arr[1].GetFloat(), 0.0f, 1.0f);
            color[2] = std::clamp(arr[2].GetFloat(), 0.0f, 1.0f);
        }
    }
}
