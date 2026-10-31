#include "UI/Panels/Frames/UIRaidFramePanel.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

std::string RaidFrameConfig::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("showSpellIcons", showSpellIcons, alloc);
    doc.AddMember("spellIconSize", spellIconSize, alloc);
    doc.AddMember("spellIconSpacing", spellIconSpacing, alloc);
    doc.AddMember("maxSpellIcons", maxSpellIcons, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

void RaidFrameConfig::fromJson(const std::string& json) {
    if (json.empty()) {
        return;
    }

    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        return;
    }

    if (doc.HasMember("showSpellIcons") && doc["showSpellIcons"].IsBool()) {
        showSpellIcons = doc["showSpellIcons"].GetBool();
    }
    if (doc.HasMember("spellIconSize") && doc["spellIconSize"].IsNumber()) {
        spellIconSize = doc["spellIconSize"].GetFloat();
        // Clamp to valid range
        if (spellIconSize < 12.0f) spellIconSize = 12.0f;
        if (spellIconSize > 32.0f) spellIconSize = 32.0f;
    }
    if (doc.HasMember("spellIconSpacing") && doc["spellIconSpacing"].IsNumber()) {
        spellIconSpacing = doc["spellIconSpacing"].GetFloat();
    }
    if (doc.HasMember("maxSpellIcons") && doc["maxSpellIcons"].IsInt()) {
        maxSpellIcons = doc["maxSpellIcons"].GetInt();
    }
}
