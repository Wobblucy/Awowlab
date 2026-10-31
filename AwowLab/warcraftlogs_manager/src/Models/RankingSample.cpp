#include "Models/RankingSample.h"

#include <rapidjson/document.h>

namespace warcraftlogs {
namespace {

// Private WCL profiles rank under this name with no report behind them.
constexpr const char* kAnonymous = "Anonymous";

std::string getString(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key)) return {};
    const auto& field = value[key];
    return field.IsString() ? std::string(field.GetString()) : std::string{};
}

int32_t getInt(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key)) return 0;
    const auto& field = value[key];
    if (field.IsInt()) return field.GetInt();
    if (field.IsDouble()) return static_cast<int32_t>(field.GetDouble());
    return 0;
}

int64_t getInt64(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key)) return 0;
    const auto& field = value[key];
    if (field.IsInt64()) return field.GetInt64();
    if (field.IsInt()) return field.GetInt();
    if (field.IsDouble()) return static_cast<int64_t>(field.GetDouble());
    return 0;
}

double getDouble(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key)) return 0.0;
    const auto& field = value[key];
    return field.IsNumber() ? field.GetDouble() : 0.0;
}

// Reads a name out of a nested object, for the guild and server blocks.
std::string getNestedString(const rapidjson::Value& parent,
                            const char* object,
                            const char* key) {
    if (!parent.HasMember(object) || !parent[object].IsObject()) return {};
    return getString(parent[object], key);
}

} // namespace

std::string parseRankingReply(const std::string& json,
                              int32_t encounterId,
                              RankingSample& out) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return "reply is not json";

    if (!doc.HasMember("data") || !doc["data"].IsObject()) return "no data";
    const auto& world = doc["data"];
    if (!world.HasMember("worldData") || !world["worldData"].IsObject()) {
        return "no worldData";
    }
    const auto& worldData = world["worldData"];
    if (!worldData.HasMember("encounter") || !worldData["encounter"].IsObject()) {
        return "no encounter";
    }
    const auto& encounter = worldData["encounter"];

    const std::string encounterName = getString(encounter, "name");

    if (!encounter.HasMember("characterRankings") ||
        !encounter["characterRankings"].IsObject()) {
        return "no characterRankings";
    }
    const auto& rankings = encounter["characterRankings"];

    // A rejected argument arrives here rather than as a failed request
    if (rankings.HasMember("error") && rankings["error"].IsString()) {
        return std::string("server refused: ") + rankings["error"].GetString();
    }

    if (!rankings.HasMember("rankings") || !rankings["rankings"].IsArray()) {
        return "rankings is not an array";
    }

    const auto& list = rankings["rankings"];
    if (list.Empty()) {
        // Nobody has ranked this boss on this spec. Worth recording so a
        // baseline can say which encounters it has nothing for.
        out.emptyEncounters.push_back(
            encounterName.empty() ? std::to_string(encounterId) : encounterName);
        return {};
    }

    for (const auto& entry : list.GetArray()) {
        if (!entry.IsObject()) continue;

        RankedPull pull;
        pull.playerName = getString(entry, "name");

        // A private profile ranks with no report behind it, so there is no
        // fight to fetch and nothing a package could be built from.
        if (pull.playerName == kAnonymous) continue;

        if (entry.HasMember("report") && entry["report"].IsObject()) {
            const auto& report = entry["report"];
            pull.reportCode = getString(report, "code");
            pull.fightId = getInt(report, "fightID");
        }
        if (pull.reportCode.empty() || pull.fightId == 0) continue;

        pull.amount = getDouble(entry, "amount");
        pull.durationMs = getInt64(entry, "duration");
        pull.itemLevel = getInt(entry, "bracketData");
        pull.serverName = getNestedString(entry, "server", "name");
        pull.serverRegion = getNestedString(entry, "server", "region");
        pull.guildName = getNestedString(entry, "guild", "name");

        // Present only when the query asked for combatant info. Each entry is
        // {talentID, points}; the id is the trait node entry HeroTalents names
        // a tree from. Absent on an older cached reply, which then simply has
        // no tree - the caller sees an empty list rather than a wrong answer.
        if (entry.HasMember("talents") && entry["talents"].IsArray()) {
            for (const auto& talent : entry["talents"].GetArray()) {
                const int32_t id = getInt(talent, "talentID");
                if (id > 0) pull.talentEntryIds.push_back(static_cast<uint32_t>(id));
            }
        }

        pull.encounterId = encounterId;
        pull.encounterName = encounterName;

        // The reply is already ordered best first, and class and spec are
        // fixed by the query, so there is nothing to sort or filter here.
        if (out.className.empty()) out.className = getString(entry, "class");
        if (out.specName.empty()) out.specName = getString(entry, "spec");

        out.pulls.push_back(std::move(pull));
    }

    return {};
}

} // namespace warcraftlogs
