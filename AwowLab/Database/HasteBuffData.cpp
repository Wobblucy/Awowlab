#include "HasteBuffData.h"
#include "Core/ErrorLogger.h"

#include <rapidjson/document.h>

#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace awow {

size_t parseHasteBuffs(std::string_view json, haste::HasteBuffTable& table) {
    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (doc.HasParseError() || !doc.IsObject() ||
        !doc.HasMember("buffs") || !doc["buffs"].IsObject()) {
        return 0;
    }

    table.clear();
    size_t loaded = 0;
    for (auto it = doc["buffs"].MemberBegin(); it != doc["buffs"].MemberEnd(); ++it) {
        const auto& entry = it->value;
        // rapidjson asserts on a Get of the wrong type, so check before reading
        if (!entry.IsObject() || !entry.HasMember("haste") || !entry["haste"].IsInt()) {
            continue;
        }

        unsigned long long spellId = 0;
        try {
            spellId = std::stoull(it->name.GetString());
        } catch (const std::exception&) {
            continue;
        }
        if (spellId == 0 || spellId > std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        haste::HasteBuffInfo info;
        info.haste_permille = entry["haste"].GetInt();
        if (entry.HasMember("stacks") && entry["stacks"].IsInt() && entry["stacks"].GetInt() > 1) {
            info.max_stacks = entry["stacks"].GetInt();
        }

        table.add(static_cast<uint32_t>(spellId), info);
        ++loaded;
    }
    return loaded;
}

bool loadHasteBuffs(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        AWOW_LOG_WARNING("DATA", "No haste buff table at " + path +
                                     " - casts will only count haste from gear");
        return false;
    }

    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    const size_t loaded = parseHasteBuffs(text, haste::HasteBuffTable::instance());
    if (loaded == 0) {
        AWOW_LOG_WARNING("DATA", "Could not read haste buffs from " + path);
        return false;
    }

    AWOW_LOG_INFO("DATA", "Loaded " + std::to_string(loaded) + " haste buffs from " + path);
    return true;
}

} // namespace awow
